import argparse
import io
import json
import math
import signal
import sys
import tomllib
import urllib.error
import urllib.request
from dataclasses import dataclass
from datetime import datetime
from importlib.resources import files
from pathlib import Path

from luma.core.interface.serial import i2c
from luma.oled.device import ssd1306
from PIL import Image, ImageDraw, ImageFont

# NOTE: relative to the working directory, i.e. the clone root; cron gets there
# with `uv run --directory`. The file is gitignored.
DEFAULT_CONFIG = Path("sittings.toml")

WIDTH, HEIGHT = 128, 64

# NOTE: from 3 h on the elapsed time shows as whole hours only.
HOURS_ONLY_FROM_MINS = 3 * 60

REQUEST_TIMEOUT = 10
# NOTE: urlopen's timeout does not cover DNS resolution; the alarm bounds the
# whole fetch so runs started every minute by cron never overlap.
DEADLINE_SECONDS = 25

SMALL_FONT_SIZE = 12
MESSAGE_FONT_SIZE = 15
# NOTE: largest size where "2h59" (widest, and "h" rises above the digits) fits
# 124 px wide and leaves 2 px gaps to the label and the bottom line (1-bit ink:
# 120x39 px).
BIG_FONT_SIZE = 50

_FONT_DATA = (files(__package__) / "fonts" / "DejaVuSans.ttf").read_bytes()
# NOTE: Pillow reads a file object to the end, so each size gets its own stream.
FONTS = {
    size: ImageFont.truetype(io.BytesIO(_FONT_DATA), size)
    for size in (SMALL_FONT_SIZE, MESSAGE_FONT_SIZE, BIG_FONT_SIZE)
}


class SittingsError(Exception):
    """A failed run. `text` goes on the panel, the message goes to stderr."""

    def __init__(self, text: str, detail: str) -> None:
        super().__init__(detail)
        self.text = text


@dataclass(frozen=True)
class Config:
    url: str
    username: str
    password: str


@dataclass(frozen=True)
class Status:
    elapsed: str
    missing: int


@dataclass(frozen=True)
class Message:
    text: str


type Screen = Status | Message


def load_config(path: Path) -> Config:
    try:
        with open(path, "rb") as f:
            data = tomllib.load(f)
    except OSError as e:
        raise SittingsError("no config", f"{path}: {e.strerror or e}") from e
    except tomllib.TOMLDecodeError as e:
        raise SittingsError("no config", f"{path}: {e}") from e

    values = {key: data.get(key) for key in ("url", "username", "password")}
    invalid = [key for key, value in values.items() if not isinstance(value, str)]
    if invalid:
        raise SittingsError("no config", f"{path}: missing or invalid {', '.join(invalid)}")
    return Config(
        url=values["url"].rstrip("/"),
        username=values["username"],
        password=values["password"],
    )


def _request(url: str, *, body: dict | None = None, token: str | None = None):
    headers = {"Accept": "application/json"}
    data = None
    if body is not None:
        data = json.dumps(body).encode()
        headers["Content-Type"] = "application/json"
    if token is not None:
        headers["Authorization"] = f"Bearer {token}"

    request = urllib.request.Request(url, data=data, headers=headers)
    with urllib.request.urlopen(request, timeout=REQUEST_TIMEOUT) as response:
        payload = response.read()
    try:
        return json.loads(payload)
    except ValueError as e:
        raise SittingsError("bad response", f"{url}: body is not JSON") from e


def _on_deadline(signum, frame):
    raise TimeoutError(f"no answer within {DEADLINE_SECONDS} s")


def fetch_daily(config: Config) -> dict:
    previous = signal.signal(signal.SIGALRM, _on_deadline)
    signal.alarm(DEADLINE_SECONDS)
    try:
        login = _request(
            f"{config.url}/api/v1/login",
            body={"username": config.username, "password": config.password},
        )
        token = login.get("token") if isinstance(login, dict) else None
        if not isinstance(token, str):
            raise SittingsError("bad response", f"{config.url}/api/v1/login: no token")
        return _request(f"{config.url}/api/v1/daily", token=token)
    except urllib.error.HTTPError as e:
        # NOTE: HTTPError subclasses URLError, so it has to be caught first. A 401 on
        # daily (token rejected) reads the same as rejected credentials.
        if e.code in (401, 403):
            raise SittingsError("login failed", f"{e.url}: HTTP {e.code}") from e
        raise SittingsError(f"server {e.code}", f"{e.url}: HTTP {e.code}") from e
    except OSError as e:
        # NOTE: covers URLError, connection errors and TimeoutError (socket or alarm).
        raise SittingsError("no network", f"{config.url}: {e}") from e
    finally:
        signal.alarm(0)
        signal.signal(signal.SIGALRM, previous)


def format_elapsed(minutes: int) -> str:
    hours, mins = divmod(minutes, 60)
    if minutes < 60:
        return f"{minutes}m"
    if minutes < HOURS_ONLY_FROM_MINS:
        return f"{hours}h{mins:02d}"
    return f"{hours}h"


def _int(value) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise TypeError(f"expected an integer, got {value!r}")
    return value


def screen_for(daily) -> Screen:
    try:
        stats = daily["stats"]
        if stats is None:
            return Message("no wakeup")

        since = stats["timeSince"]
        if since is None:
            elapsed = _int(stats["fromWakeupMins"])
        else:
            hours, mins = since
            elapsed = _int(hours) * 60 + _int(mins)

        debt = stats["sittingDebt"]
        if not isinstance(debt, (int, float)) or isinstance(debt, bool):
            raise TypeError(f"expected a number for sittingDebt, got {debt!r}")
    except (KeyError, TypeError, ValueError) as e:
        raise SittingsError("bad response", f"unexpected daily response: {e!r}") from e

    # NOTE: clamp so clock skew between server and Pi never shows a negative time.
    return Status(format_elapsed(max(0, elapsed)), max(0, math.floor(debt)))


def lines(screen: Screen) -> list[str]:
    if isinstance(screen, Message):
        return [screen.text]
    return [f"since {screen.elapsed}", f"missing {screen.missing}"]


def _ink_bbox(text: str, font: ImageFont.FreeTypeFont) -> tuple[int, int, int, int]:
    # NOTE: textbbox measures the anti-aliased outline; 1-bit glyphs can end a pixel
    # or two short of it, which shows as off-centre big text. Measure real ink.
    origin = 32
    scratch = Image.new("1", (WIDTH * 2, HEIGHT * 2))
    draw = ImageDraw.Draw(scratch)
    draw.fontmode = "1"
    draw.text((origin, origin), text, font=font, fill="white")
    left, top, right, bottom = scratch.getbbox()
    return left - origin, top - origin, right - origin, bottom - origin


def render(screen: Screen) -> Image.Image:
    image = Image.new("1", (WIDTH, HEIGHT))
    draw = ImageDraw.Draw(image)
    # NOTE: monochrome FreeType rendering keeps glyphs crisp on a 1-bit panel.
    draw.fontmode = "1"

    if isinstance(screen, Message):
        font = FONTS[MESSAGE_FONT_SIZE]
        left, top, right, bottom = _ink_bbox(screen.text, font)
        x = (WIDTH - (right - left)) // 2 - left
        y = (HEIGHT - (bottom - top)) // 2 - top
        draw.text((x, y), screen.text, font=font, fill="white")
        return image

    small = FONTS[SMALL_FONT_SIZE]

    # NOTE: the label's ink sits on the top edge, the bottom line's on the bottom edge.
    label = "since"
    left, top, right, bottom = _ink_bbox(label, small)
    draw.text((-left, -top), label, font=small, fill="white")
    band_top = bottom - top

    missing = f"missing {screen.missing}"
    left, top, right, bottom = _ink_bbox(missing, small)
    draw.text((-left, HEIGHT - bottom), missing, font=small, fill="white")
    band_bottom = HEIGHT - (bottom - top)

    big = FONTS[BIG_FONT_SIZE]
    left, top, right, bottom = _ink_bbox(screen.elapsed, big)
    x = (WIDTH - (right - left)) // 2 - left
    y = band_top + (band_bottom - band_top - (bottom - top)) // 2 - top
    draw.text((x, y), screen.elapsed, font=big, fill="white")
    return image


def show_on_panel(screen: Screen) -> None:
    serial = i2c(port=1, address=0x3C)
    device = ssd1306(serial)
    device.display(render(screen))
    # NOTE: see hello; keeps the frame on the panel between cron runs.
    device.persist = True


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Show the sitting tracker status on the SSD1306 panel."
    )
    parser.add_argument(
        "--config",
        type=Path,
        default=DEFAULT_CONFIG,
        metavar="PATH",
        help=f"TOML file with url, username and password (default: {DEFAULT_CONFIG})",
    )
    parser.add_argument(
        "--print",
        action="store_true",
        help="print the status to the terminal instead of drawing it on the panel",
    )
    args = parser.parse_args()

    failed = False
    try:
        screen = screen_for(fetch_daily(load_config(args.config)))
    except SittingsError as e:
        failed = True
        screen = Message(e.text)
        now = datetime.now().isoformat(timespec="seconds")
        print(f"{now} sittings: {e.text}: {e}", file=sys.stderr)

    if args.print:
        print("\n".join(lines(screen)))
    else:
        show_on_panel(screen)

    if failed:
        sys.exit(1)

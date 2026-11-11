import argparse
import io
import math
import time
from importlib.resources import files

from luma.core.interface.serial import i2c
from luma.oled.device import ssd1306
from PIL import Image, ImageDraw, ImageFont

TEXT = "Привіт, пацики"

WIDTH, HEIGHT = 128, 64
CENTER_X, CENTER_Y = WIDTH // 2, HEIGHT // 2

# NOTE: all times are in seconds from the start of the animation.
CIRCLE_RADIUS = 12
COLLAPSE_START = 0.3
COLLAPSE_END = 0.7
DOT_COUNT = 12
DOT_SIZE = 2
DOT_SPEED = 75  # px/s; clears the farthest corner (~72 px) by t = 1.3
TEXT_START = 1.1
END = 2.0
MIN_FONT_SIZE = 4
FINAL_FONT_SIZE = 15

PANEL_FPS = 30
GIF_FRAME_SECONDS = 0.04
GIF_SCALE = 4
GIF_FINAL_HOLD_SECONDS = 1.5

_FONT_DATA = (files(__package__) / "fonts" / "DejaVuSans.ttf").read_bytes()
# NOTE: Pillow reads a file object to the end, so each size gets its own stream.
FONTS = {
    size: ImageFont.truetype(io.BytesIO(_FONT_DATA), size)
    for size in range(MIN_FONT_SIZE, FINAL_FONT_SIZE + 1)
}


def circle_radius(t: float) -> float:
    if t < COLLAPSE_START:
        return CIRCLE_RADIUS
    progress = (t - COLLAPSE_START) / (COLLAPSE_END - COLLAPSE_START)
    return CIRCLE_RADIUS * max(0.0, 1 - progress)


def dot_positions(t: float) -> list[tuple[float, float]]:
    if t < COLLAPSE_START:
        return []
    distance = DOT_SPEED * (t - COLLAPSE_START)
    return [
        (
            CENTER_X + distance * math.cos(2 * math.pi * k / DOT_COUNT),
            CENTER_Y + distance * math.sin(2 * math.pi * k / DOT_COUNT),
        )
        for k in range(DOT_COUNT)
    ]


def font_size(t: float) -> int | None:
    if t < TEXT_START:
        return None
    steps = FINAL_FONT_SIZE - MIN_FONT_SIZE + 1
    progress = min((t - TEXT_START) / (END - TEXT_START), 1.0)
    return MIN_FONT_SIZE + min(int(progress * steps), steps - 1)


def render(t: float) -> Image.Image:
    image = Image.new("1", (WIDTH, HEIGHT))
    draw = ImageDraw.Draw(image)
    # NOTE: monochrome FreeType rendering keeps small glyphs crisp on a 1-bit panel.
    draw.fontmode = "1"

    radius = circle_radius(t)
    if radius >= 0.5:
        draw.ellipse(
            (CENTER_X - radius, CENTER_Y - radius, CENTER_X + radius, CENTER_Y + radius),
            fill="white",
        )

    for x, y in dot_positions(t):
        left = round(x - DOT_SIZE / 2)
        top = round(y - DOT_SIZE / 2)
        draw.rectangle((left, top, left + DOT_SIZE - 1, top + DOT_SIZE - 1), fill="white")

    size = font_size(t)
    if size is not None:
        font = FONTS[size]
        left, top, right, bottom = draw.textbbox((0, 0), TEXT, font=font)
        x = (WIDTH - (right - left)) // 2 - left
        y = (HEIGHT - (bottom - top)) // 2 - top
        draw.text((x, y), TEXT, font=font, fill="white")

    return image


def play_on_panel() -> None:
    serial = i2c(port=1, address=0x3C)
    device = ssd1306(serial)
    frame_seconds = 1 / PANEL_FPS

    start = time.monotonic()
    while True:
        t = min(time.monotonic() - start, END)
        device.display(render(t))
        if t == END:
            break
        # NOTE: a frame slower than one tick (slow I2C) skips the sleep, so frames
        # drop instead of the animation slowing down.
        next_tick = start + (math.floor(t / frame_seconds) + 1) * frame_seconds
        time.sleep(max(0.0, next_tick - time.monotonic()))

    # NOTE: see hello; keeps the final frame on the panel after exit.
    device.persist = True


def write_gif(path: str) -> None:
    frame_count = round(END / GIF_FRAME_SECONDS)
    size = (WIDTH * GIF_SCALE, HEIGHT * GIF_SCALE)
    frames = [
        render(END * i / frame_count).resize(size, Image.Resampling.NEAREST)
        for i in range(frame_count + 1)
    ]
    durations = [round(GIF_FRAME_SECONDS * 1000)] * frame_count
    durations.append(round(GIF_FINAL_HOLD_SECONDS * 1000))
    frames[0].save(
        path, save_all=True, append_images=frames[1:], duration=durations, loop=0
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Play the splash animation on the SSD1306 panel."
    )
    parser.add_argument(
        "--gif",
        metavar="PATH",
        help="write the animation to an animated GIF instead of driving the panel",
    )
    args = parser.parse_args()

    if args.gif:
        write_gif(args.gif)
    else:
        play_on_panel()

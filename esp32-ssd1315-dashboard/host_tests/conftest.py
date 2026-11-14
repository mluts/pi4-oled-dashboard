"""Builds libdash_host.dylib (see CMakeLists.txt) and hands it to the tests."""

import ctypes
import shutil
import subprocess
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path

import pytest
from PIL import Image

HERE = Path(__file__).parent
BUILD = HERE.parent / "build" / "host_tests"

WIDTH = 128
HEIGHT = 64


class Font(IntEnum):
    """Same order as kFonts in shim.cpp."""

    SMALL = 0
    BIG = 1
    MESSAGE = 2
    PROMPT = 3


@dataclass(frozen=True)
class Band:
    """Consecutive rows with lit pixels, and the columns their ink spans."""

    top: int
    bottom: int
    left: int
    right: int


class Frame:
    """A copy of u8g2's frame buffer as rows of pixels."""

    def __init__(self, raw: bytes):
        # NOTE: u8g2's page layout: 8 pages of 128 bytes, each byte a column
        # of 8 pixels with the top one in bit 0.
        self.pixels = [
            [bool(raw[(y // 8) * WIDTH + x] >> (y % 8) & 1) for x in range(WIDTH)]
            for y in range(HEIGHT)
        ]

    def ink(self) -> int:
        return sum(sum(row) for row in self.pixels)

    def bands(self) -> list[Band]:
        bands: list[Band] = []
        for y, row in enumerate(self.pixels):
            lit = [x for x, on in enumerate(row) if on]
            if not lit:
                continue
            if bands and bands[-1].bottom == y - 1:
                last = bands.pop()
                bands.append(Band(last.top, y, min(last.left, lit[0]), max(last.right, lit[-1])))
            else:
                bands.append(Band(y, y, lit[0], lit[-1]))
        return bands

    def save_png(self, path: Path, scale: int = 4) -> None:
        """White on black like the panel, scaled up so pixels are visible."""
        image = Image.new("1", (WIDTH, HEIGHT))
        image.putdata([255 if on else 0 for row in self.pixels for on in row])
        path.parent.mkdir(parents=True, exist_ok=True)
        image.resize((WIDTH * scale, HEIGHT * scale), Image.NEAREST).save(path)


class DashResult(ctypes.Structure):
    """Mirrors DashResult in shim.cpp, field by field."""

    _fields_ = [
        ("kind", ctypes.c_int),  # 0 status, 1 message
        ("elapsed", ctypes.c_char * 8),
        ("missing", ctypes.c_int),
        ("line1", ctypes.c_char * 33),
        ("error", ctypes.c_char * 16),
    ]


def _declare(lib: ctypes.CDLL) -> None:
    """Argument and return types of shim.cpp's functions; ctypes would assume
    int everywhere, which truncates the buffer pointer."""
    c_int3 = ctypes.c_int * 3
    lib.dash_init.argtypes = []
    lib.dash_init.restype = None
    lib.dash_buffer.argtypes = []
    lib.dash_buffer.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.dash_draw_status.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_bool]
    lib.dash_draw_status.restype = None
    lib.dash_draw_message.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_bool]
    lib.dash_draw_message.restype = None
    lib.dash_status_layout.argtypes = [c_int3]
    lib.dash_status_layout.restype = None
    lib.dash_message_layout.argtypes = [c_int3]
    lib.dash_message_layout.restype = None
    lib.dash_prompt_baseline.argtypes = []
    lib.dash_prompt_baseline.restype = ctypes.c_int
    lib.dash_draw_text.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_char_p]
    lib.dash_draw_text.restype = ctypes.c_int
    lib.dash_format_elapsed.argtypes = [ctypes.c_int, ctypes.c_char_p]
    lib.dash_format_elapsed.restype = None
    lib.dash_screen_from_daily.argtypes = [
        ctypes.c_char_p,
        ctypes.c_int,
        ctypes.POINTER(DashResult),
    ]
    lib.dash_screen_from_daily.restype = ctypes.c_int


@pytest.fixture(scope="session")
def lib() -> ctypes.CDLL:
    """The shim, built once per session; a no-op build when nothing changed."""
    if shutil.which("ninja") is None:
        pytest.exit("ninja not found: activate the IDF environment or brew install ninja", 2)
    subprocess.run(["cmake", "-S", HERE, "-B", BUILD, "-G", "Ninja"], check=True)
    subprocess.run(["cmake", "--build", BUILD], check=True)

    lib = ctypes.CDLL(BUILD / "libdash_host.dylib")
    _declare(lib)
    lib.dash_init()
    return lib


class Panel:
    """Draws through the shim and returns Frames; every screen is also saved
    as a PNG under build/host_tests/frames/ for looking at."""

    # Where text() draws a string alone: every font fits at this baseline.
    ALONE_BASELINE = 40

    def __init__(self, lib: ctypes.CDLL):
        self._lib = lib

    def _frame(self) -> Frame:
        return Frame(ctypes.string_at(self._lib.dash_buffer(), WIDTH * HEIGHT // 8))

    def _saved(self, *name_parts: object) -> Frame:
        frame = self._frame()
        name = "-".join(str(p) for p in name_parts if p != "")
        safe = "".join(c if c.isalnum() else "-" for c in name)
        frame.save_png(BUILD / "frames" / f"{safe}.png")
        return frame

    def status(self, elapsed: str, missing: int, prompt: bool = False) -> Frame:
        self._lib.dash_draw_status(elapsed.encode(), missing, prompt)
        return self._saved("status", elapsed, f"missing{missing}", "prompt" if prompt else "")

    def message(self, line1: str, line2: str = "", prompt: bool = False) -> Frame:
        self._lib.dash_draw_message(line1.encode(), line2.encode(), prompt)
        return self._saved("message", line1, line2, "prompt" if prompt else "")

    def text(self, font: Font, text: str) -> tuple[Frame, int]:
        """One string alone at x 0; the frame and the string's width."""
        width = self._lib.dash_draw_text(font, 0, self.ALONE_BASELINE, text.encode())
        return self._frame(), width

    def status_layout(self) -> tuple[int, int, int]:
        """Baselines: label, big, bottom."""
        out = (ctypes.c_int * 3)()
        self._lib.dash_status_layout(out)
        return tuple(out)

    def message_layout(self) -> tuple[int, int, int]:
        """Baselines: single, first, second."""
        out = (ctypes.c_int * 3)()
        self._lib.dash_message_layout(out)
        return tuple(out)

    def prompt_baseline(self) -> int:
        return self._lib.dash_prompt_baseline()


@pytest.fixture(scope="session")
def panel(lib: ctypes.CDLL) -> Panel:
    return Panel(lib)

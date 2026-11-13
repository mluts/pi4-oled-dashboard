"""The painter and the layout, checked on the frames draw.cpp produces."""

import pytest

from conftest import WIDTH, Band, Font, Panel

ELAPSED = ["0m", "59m", "1h05", "2h59", "5h", "23h"]


def expected_ink(panel: Panel, texts: list[tuple[Font, str]]) -> int:
    """Lit pixels of the strings each drawn alone. A frame showing all of them
    has fewer only when something was cut off at an edge (the strings of one
    screen never overlap)."""
    total = 0
    for font, text in texts:
        frame, width = panel.text(font, text)
        assert width <= WIDTH, f"{text!r} is {width} px wide"
        total += frame.ink()
    return total


@pytest.mark.parametrize(
    "elapsed, prompt", [(e, False) for e in ELAPSED] + [("2h59", True)]
)
def test_status_not_clipped(panel: Panel, elapsed: str, prompt: bool):
    texts = [(Font.SMALL, "since"), (Font.BIG, elapsed), (Font.SMALL, "missing 2")]
    if prompt:
        texts.append((Font.PROMPT, "refresh?"))
    expected = expected_ink(panel, texts)

    assert panel.status(elapsed, 2, prompt).ink() == expected


@pytest.mark.parametrize(
    "line1, line2, prompt",
    [
        ("no wakeup", "", False),
        ("bad response", "", False),
        ("no network", "", True),
        ("MyHomeWiFi", "try 3", False),
    ],
)
def test_message_not_clipped(panel: Panel, line1: str, line2: str, prompt: bool):
    font = Font.SMALL if line2 else Font.MESSAGE
    texts = [(font, line1)] + ([(font, line2)] if line2 else [])
    if prompt:
        texts.append((Font.PROMPT, "refresh?"))
    expected = expected_ink(panel, texts)

    assert panel.message(line1, line2, prompt).ink() == expected


def big_band(bands: list[Band]) -> Band:
    """The elapsed time: the tallest band. The small lines can split into more
    than one band (the dot of an i sits above an empty row)."""
    return max(bands, key=lambda b: b.bottom - b.top)


@pytest.mark.parametrize("elapsed", ELAPSED)
def test_big_line_gaps_and_centred(panel: Panel, elapsed: str):
    bands = panel.status(elapsed, 2).bands()
    big = big_band(bands)
    above = max(b.bottom for b in bands if b.bottom < big.top)
    below = min(b.top for b in bands if b.top > big.bottom)

    assert big.top - above - 1 >= 2, "gap above the big line"
    assert below - big.bottom - 1 >= 2, "gap below the big line"
    # NOTE: measured on the ink, not GetStrWidth: the advance width includes
    # the glyphs' empty side bearings.
    left, right = big.left, WIDTH - 1 - big.right
    assert abs(left - right) <= 2, f"margins {left} / {right}"


def test_layout_baselines(panel: Panel):
    label, big, bottom = panel.status_layout()
    assert label < big < bottom <= 63
    _, first, second = panel.message_layout()
    assert first < second
    assert panel.prompt_baseline() < 8

    # What StatusLayout relies on (layout.cpp): u8g2 leaves the baseline row
    # empty, and logisoso's round digits dip into it.
    assert big_band(panel.status("2h59", 2).bands()).bottom == big - 1
    assert big_band(panel.status("23h", 2).bands()).bottom == big

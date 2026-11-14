"""status.cpp: the /api/v1/daily body mapped to a screen, checked through the
shim with bodies shaped like the server's (sittings/openapi.json)."""

import ctypes
import json

import pytest

from conftest import DashResult


def stats(**overrides) -> dict:
    """All twelve required stats fields; the firmware reads only three."""
    base = {
        "nSittings": 3,
        "toSitMax": 8,
        "toSitMaxFlt": 8.0,
        "toSitTimes": 5.0,
        "sittingDebt": 2.6,
        "countdownMins": None,
        "ratioGoal": 50,
        "fromWakeupMins": 300,
        "sittedMins": 90,
        "unsittedMins": 210,
        "timeSince": [2, 15],
        "lastTime": [10, 45],
    }
    return base | overrides


def daily(stats: dict | None, sittings: int = 0) -> str:
    return json.dumps(
        {
            "lastSittingGoal": 8,
            "settings": {
                "sittingDuration": 30,
                "sittingInterleaving": False,
                "sittingInterleavingGap": 0,
                "preferredLang": "uk",
                "preferredTheme": "dark",
                "preferredColor": "blue",
            },
            "sittings": [
                {"sittingId": i, "sittingsN": i, "at": f"2026-11-14T08:{i % 60:02d}:00Z"}
                for i in range(sittings)
            ],
            "stats": stats,
            "wakeup": {
                "createdAt": "2026-11-14T06:00:00Z",
                "sittingGoal": 8,
                "updatedAt": "2026-11-14T06:00:00Z",
                "wakeupDate": "2026-11-14",
                "wakeupTime": "07:00:00",
            },
        }
    )


def parse(lib: ctypes.CDLL, body: str) -> tuple:
    """("status", elapsed, missing), ("message", line1) or ("error", text)."""
    raw = body.encode()
    out = DashResult()
    if not lib.dash_screen_from_daily(raw, len(raw), ctypes.byref(out)):
        return ("error", out.error.decode())
    if out.kind == 0:
        return ("status", out.elapsed.decode(), out.missing)
    return ("message", out.line1.decode())


@pytest.mark.parametrize(
    "minutes, text",
    [
        (0, "0m"),
        (5, "5m"),
        (59, "59m"),
        (60, "1h00"),
        (65, "1h05"),
        (179, "2h59"),
        (180, "3h"),
        (347, "5h"),
        (1439, "23h"),
    ],
)
def test_format_elapsed(lib: ctypes.CDLL, minutes: int, text: str):
    out = ctypes.create_string_buffer(8)
    lib.dash_format_elapsed(minutes, out)
    assert out.value.decode() == text


@pytest.mark.parametrize("debt, missing", [(2.6, 2), (0.3, 0), (-0.8, 0)])
def test_status_from_time_since(lib: ctypes.CDLL, debt: float, missing: int):
    body = daily(stats(timeSince=[2, 15], sittingDebt=debt))
    assert parse(lib, body) == ("status", "2h15", missing)


def test_before_first_sitting(lib: ctypes.CDLL):
    body = daily(stats(timeSince=None, fromWakeupMins=100))
    assert parse(lib, body)[1] == "1h40"


def test_no_wakeup(lib: ctypes.CDLL):
    assert parse(lib, daily(None)) == ("message", "no wakeup")


def test_large_response(lib: ctypes.CDLL):
    body = daily(stats(timeSince=[2, 15], sittingDebt=2.6), sittings=100)
    # NOTE: about the size design.md sizes the 16 KB body buffer for.
    assert len(body) > 7000
    assert parse(lib, body) == ("status", "2h15", 2)


@pytest.mark.parametrize(
    "body",
    [
        "<html>502 Bad Gateway</html>",
        json.dumps({"settings": {}, "sittings": []}),
        daily(stats(sittingDebt="x")),
        daily(stats(timeSince=[1])),
    ],
    ids=["not-json", "missing-stats", "debt-string", "time-since-one"],
)
def test_bad_response(lib: ctypes.CDLL, body: str):
    assert parse(lib, body) == ("error", "bad response")

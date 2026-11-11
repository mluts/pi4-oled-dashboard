# Design

## Context

See proposal.md for the motivation and specs/sitting-status/spec.md for the required behavior. These are the facts that shape the approach:

- Existing commands (`hello` in `__init__.py`, `splash` in `splash.py`) open the panel with `i2c(port=1, address=0x3C)` + `ssd1306(serial)`, draw, and set `device.persist = True`. `splash` builds frames as mode-"1" `PIL.Image`s in a pure `render(t)` and calls `device.display(img)`. It also bundles DejaVu Sans, loaded via `importlib.resources`.
- Python is 3.13 on both the Mac and the Pi, so `tomllib` is available. The only runtime dependency is `luma-oled`, which brings in Pillow. The project has no test suite; earlier changes verify with `.venv/bin/python -c` and scratchpad scripts.
- API (`sittings/openapi.json`):
  - `POST /api/v1/login {username, password}` returns `{token, username}`.
  - `GET /api/v1/daily` returns `{wakeup, sittings, settings, stats, lastSittingGoal}`.
  - `stats` is nullable. It has `timeSince: [int, int] | null`, `fromWakeupMins: int` and `sittingDebt: number`.
  - The spec declares no security scheme, so how the token is sent is unknown.
- At planning time the API was not deployed. Its runtime behavior was later observed on a local dev server running the same API (see the "Observed" notes below).
- Cron runs the command with a minimal environment. `uv` is not on its `PATH`, shell exports are not visible, and stdout goes nowhere unless redirected.

## Goals / Non-Goals

**Goals:**
- Every run is self-contained and stateless: no cache file, no token store, no daemon.
- One pure function decides what to show and another draws it, so the formatting rules and the layout can be checked on the Mac without the API or the panel.
- Every failure ends up as a short code on the panel and a detailed line on stderr.

**Non-Goals:**
- Any write to the API (logging sittings, setting wakeup, changing settings).
- Caching the token or the last good status between runs.
- Searching for the config in several places, enforcing file permissions, or a keyring. There is exactly one default location and the README recommends `chmod 600`.
- Burn-in mitigation such as pixel shifting or dimming. That can be a later change once the screen is in daily use.
- Installing the crontab entry automatically. It is documented only.

## Decisions

### One module, three pure stages and two outputs

```
load_config(path) --> fetch_daily(config) --> screen_for(daily) --> Screen
        |                    |                       |                  |
        +------ raise SittingsError("no config" / "no network" / ...) ---+
                                                                       |
                                          +----------------------------+
                                          v                            v
                                 render(screen) -> Image        lines(screen) -> [str]
                                          |                            |
                                 device.display, persist        print   (--print)
```

- `Screen` is either a status (elapsed text + missing count) or a message (one short text). `screen_for` holds all the decision rules from the spec: no wakeup, the wakeup fallback, the time format and the debt rounding. It takes the parsed JSON dict, so it can be exercised with hand-written dicts.
- `SittingsError` carries the panel text. `main` catches it, builds a message `Screen`, outputs it, writes the detail to stderr and exits 1. Every other exception, including I2C failures, propagates as in `hello`.
- The device is opened only after the screen has been decided. A slow fetch never holds the bus open, and an error is drawn the same way as a status.
- *Alternative*: one function that fetches and draws. That is fewer lines, but the rules can then only be checked against a live server.

### HTTP with `urllib.request`, logging in on every run
- Login: POST a JSON body with `Content-Type: application/json`. Daily: GET with the token attached. Each request gets `timeout=10`.
- Token: sent as `Authorization: Bearer <token>`. *Observed (task 2.1, local dev server)*: login returns `{token, username}` and sets no cookie, `daily` accepts the Bearer header, and both a bad token and a wrong password get `401 {"error":"unauthorized"}`.
- No token cache: one login a minute is negligible load, and staying stateless means an expired token cannot break the screen.
- *Alternative*: `httpx`/`requests`. They make nothing here easier and would add a dependency to sync on the Pi.

### A hard 25-second deadline per run
`signal.alarm(25)` is set inside `fetch_daily`, around both requests. A `SIGALRM` handler raises `TimeoutError`, which maps to `no network`. Config loading stays outside the alarm: it is a local file read, and a timeout there would be misreported as `no config`.

- *Why*: the per-request `timeout` does not cover DNS resolution (`getaddrinfo` blocks outside the socket timeout). The deadline guarantees the 30-second bound from the spec, leaving about 5 s to draw. That way, runs started every minute never pile up on the I2C bus.
- *Alternative*: running the fetch in a thread with a join timeout. That works on any platform, but it is more code, and `signal.alarm` is fine on the Pi and the Mac.

### Error mapping

| Raised during fetch                                     | Panel text      |
|---------------------------------------------------------|-----------------|
| config `FileNotFoundError` / `PermissionError` / `TOMLDecodeError` / missing key | `no config` |
| `HTTPError` with 401 or 403 (login or daily)            | `login failed`  |
| `HTTPError`, any other code                             | `server <code>` |
| `URLError`, `TimeoutError`, other `OSError`             | `no network`    |
| invalid JSON, missing or mistyped fields                | `bad response`  |

`HTTPError` is a subclass of `URLError`, so it is caught first. A 401 on `daily` also maps to `login failed`, which is what a wrong token scheme looks like. The stderr line contains the exception text and never the password or token.

### Mapping the daily response to a screen
1. `stats is null` → message `no wakeup`.
2. `stats.timeSince` is `[h, m]` → elapsed = `h*60 + m` minutes. If it is `null` → elapsed = `stats.fromWakeupMins`.
3. Format the elapsed minutes so every value carries a unit: under 60 → `f"{minutes}m"`, under 180 → `f"{h}h{m:02d}"`, otherwise `f"{h}h"` (rounded down by integer division). The first version used `h:mm` and a bare `h`. On the panel a bare `5` couldn't be told apart from minutes, so the units were added after the first on-device run.
4. missing = `max(0, math.floor(stats.sittingDebt))`.

- *Observed (task 2.1, local dev server)*: with a wakeup set 1 h 40 min earlier, `fromWakeupMins` was 100 and `timeSince` was `null`. Right after logging a sitting, `timeSince` was `[0, 0]` and `lastTime` was `[14, 54]`, the clock time of that sitting. So `timeSince` is the elapsed `[hours, minutes]` and `lastTime` is the clock time. The sitting moved `sittingDebt` from 1.8 to 0.7.
- *Why use server-side values*: the server already knows the user's day boundary and timezone. Computing from `at` on the Pi would duplicate that logic.

### Layout on the 128x64 panel

```
+----------------------------+
|since                       |  12 px DejaVu, top-left
|                            |
|          2h15              |  big DejaVu, centered by ink bbox
|                            |  in the band between the two lines
|missing 2                   |  12 px DejaVu, bottom-left
+----------------------------+
```

- Big font size: the largest integer size at which the widest elapsed string has an ink width of at most 124 px, *and* its ink height fits between the label and the bottom line with a gap of at least 2 px on each side. The `since` label's ink takes rows 0–8 and `missing N` takes rows 52–63, so the big text's ink can be at most 39 rows tall.
  - *Measured (task 1.7, 1-bit rendering)*: the widest string is `2h59`, and the `h` ascender rises a row above the digits. The chosen size is **50 px**, where `2h59` is 120x39 px of ink with 2 blank rows above and below. At 51 px it would be 40 rows tall.
  - The first `h:mm` version (task 1.3) fitted `"0:00"` at 51 px.
- Every element is placed by its real 1-bit ink box, measured by rendering into a scratch image, not by `textbbox`. `textbbox` measures the anti-aliased outline, and 1-bit glyphs can end 1–2 px short of it, which made `0m` sit 1.5 px off-centre.
- Message screens (`no wakeup`, errors) show one line at 15 px, centered, the same as the final `splash` text. `bad response` is the longest and fits easily.
- `draw.fontmode = "1"` and loading through `importlib.resources`, both as in `splash`. The font is shared by reading the same `fonts/DejaVuSans.ttf`. Nothing is extracted into a common module yet; two call sites don't justify it.
- The `since` label stays the same before the first sitting (when the time counts from wakeup). The spec chose a layout that keeps the same shape all day.

### CLI
`argparse` with `--config PATH` and `--print`. `--config` defaults to `Path("sittings.toml")`, relative to the working directory. `--print` writes `lines(screen)` to stdout, either `since 2h15` + `missing 2` or the single message. It never constructs the device. On success, nothing is written to stdout unless `--print` is given, so cron stays quiet.

### Cron entry (documented in the README)

```
* * * * * $HOME/.local/bin/uv run --quiet --directory $HOME/pi4-oled-dashboard sittings 2>>$HOME/sittings.log
```

- An absolute path to `uv` is needed because cron's `PATH` does not include `~/.local/bin`. `--quiet` hides uv's own sync messages, so the log only gets the command's error lines.
- `--directory` makes uv change into the clone before it runs the command. uv then finds the project there, and the relative default `sittings.toml` resolves to the clone root.

### Config file in the clone, gitignored
The real `sittings.toml` sits in the repository root, next to a committed `sittings.example.toml` that holds the production `url` and placeholder credentials. `.gitignore` lists `/sittings.toml`.

- *Why*: the operator keeps everything for the dashboard in one place, and setting up a Pi is just `cp`, `chmod` and editing the file.
- *Why the default is relative to the working directory*: the path doesn't depend on how the package is installed. Resolving it from `__file__` would only find the clone root in an editable install. Cron and the README both run the command from the clone root.
- *Alternative*: `~/.config/pi-ssd1306-dashboard/sittings.toml`. It survives deleting the clone, but it's another place to remember. The user chose the in-repo location.
- After a `git pull`, the next cron run syncs dependencies itself, so no separate step is needed on the Pi.

## Risks / Trade-offs

- [Production differs from the local dev server] The token scheme and field meanings were checked locally only. → Task 3.1 runs against production on the Pi. A mismatch shows up as `login failed` or `bad response`, not as wrong numbers.
- [Production is served over HTTPS only and redirects plain HTTP] urllib turns a redirected POST into a GET, so the login would fail. → The README's config example uses `https://`.
- [Log growth] During an outage, one stderr line a minute is about 1,440 lines a day. → Acceptable. A successful run writes nothing.
- [Burn-in] The label and the `missing` line are static for hours. → Accepted for now (see Non-Goals).
- [Overlapping runs] → The 25-second deadline keeps each run under a minute.

## Migration Plan

Additive only. To deploy on the Pi: `git pull`, copy `sittings.example.toml` to `sittings.toml` and fill it in, then add the crontab line. To roll back: remove the crontab line. The last frame stays on the panel until the next draw or until power is lost.


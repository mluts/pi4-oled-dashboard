# Tasks

## 1. Offline implementation (Mac, no live API needed)

- [x] 1.1 Create `src/pi_ssd1306_dashboard/sittings.py` with `SittingsError` (carries the panel text), `load_config(path)` (tomllib; requires `url`, `username`, `password`; strips a trailing `/` from `url`) and `screen_for(daily)`, following design.md "Mapping the daily response to a screen". Verify with `.venv/bin/python -c`:
  - Elapsed formatting: `timeSince` `[0, 5]` → `0:05`, `[2, 59]` → `2:59`, `[3, 0]` → `3`, `[5, 47]` → `5`.
  - Debt: `sittingDebt` 2.6 → missing 2, 0.3 → 0, -0.8 → 0.
  - `stats: null` → message `no wakeup`.
  - `timeSince: null` with `fromWakeupMins: 100` → `1:40`.
  - A dict without `stats`, or with `sittingDebt: "x"` → `SittingsError("bad response")`.
  - A missing file, a file missing `password`, and invalid TOML (scratchpad files) → `SittingsError("no config")`.
- [x] 1.2 Add `fetch_daily(config)`: `urllib.request` login POST (JSON body) then the daily GET with `Authorization: Bearer <token>`, `timeout=10` per request, the 25 s `signal.alarm` deadline, and the error mapping table from design.md. Write a stub server in the scratchpad (`http.server`, run in the background) that can answer each case, and verify:
  - A normal login + daily returns the parsed dict, and the stub saw a JSON body with `username`/`password` and the header `Authorization: Bearer <token>`.
  - 401 on login → `login failed`.
  - 401 on daily → `login failed`.
  - 502 on daily → `server 502`.
  - A non-JSON 200 → `bad response`.
  - `url = "http://127.0.0.1:9"` → `no network`.
  - A handler that accepts and never answers → `no network`, and the call returns in under 30 s (time it).
  - No stderr detail contains the password or the token.
- [x] 1.3 Add `render(screen)` (128x64 mode "1", DejaVu via `importlib.resources`, `fontmode = "1"`) and `lines(screen)`, laid out as in design.md "Layout on the 128x64 panel". Measure and pick the big font size by that rule, and record the chosen size and the measured `"0:00"` ink width and height in design.md. Verify with `.venv/bin/python -c`:
  - For `0:00`, `2:59`, `5` with `missing 2`, the lit pixels stay inside the panel.
  - The big text's ink bounding box is horizontally centered within ±1 px.
  - There are at least 2 px of blank rows between the label, the big text and the `missing` line.
  - `no wakeup` and `bad response` are centered and not cut off.

  Save 4x-upscaled PNGs of these screens to the scratchpad and open them to review the layout.
- [x] 1.4 Add `main()` with `argparse` (`--config PATH` and `--print`; the default config location is set in 1.6). On `SittingsError`, write the detail to stderr and exit 1. Without `--print`, open the device as `splash` does, then `device.display(render(screen))` and set `device.persist = True`. Register `sittings = "pi_ssd1306_dashboard.sittings:main"` under `[project.scripts]`. Verify on the Mac, against the stub from 1.2:
  - `uv run sittings --print --config <stub config>` prints `since 2:15` and `missing 2` and exits 0.
  - `uv run sittings --print --config /nonexistent.toml` prints `no config` and exits non-zero.
  - `uv run sittings --config <stub config>` (no `--print`) fails with an error about `/dev/i2c-1` and exits non-zero.

  Check exit statuses with `echo $status` in fish.
- [x] 1.5 Update `README.md`:
  - The intro line mentions the `sittings` screen.
  - A `sittings` section showing how to create the config file (the three keys, `chmod 600`; the location is set in 1.6).
  - `uv run sittings --print` for testing on the Mac.
  - What the panel shows: the format rules, `no wakeup`, and the error texts.
  - The crontab line from design.md, with a note on `$HOME/sittings.log`.

  Verify that every new command in the README is copy-pasteable as written.
- [x] 1.6 Move the config into the repository, as design.md "Config file in the clone, gitignored" describes:
  - Add a commented `sittings.example.toml` to the repository root, with the production `url` and placeholder credentials.
  - Add `/sittings.toml` to `.gitignore`.
  - Change `--config`'s default to `sittings.toml` in the working directory.
  - Change the README's config steps to `cp sittings.example.toml sittings.toml` + `chmod 600`, and the crontab line to `uv run --directory`.

  Verify that:
  - `load_config` accepts the example file as is.
  - `llm-git check-ignore` reports `sittings.toml` as ignored.
  - With a `sittings.toml` in the repository root pointing at the local API, `uv run sittings --print` with no `--config` prints the status.
  - The crontab form `uv run --quiet --directory <clone> sittings --print` works from outside the clone.

## 2. Live API check (needs the API deployed)

- [x] 2.1 The user provides credentials for a reachable API on the Mac (done against the local dev server at `http://localhost:3010`). If the API is not reachable yet, stop and ask the user. Run a scratchpad probe script that reads the config file itself (the password never goes on a command line or into output) and checks the following:
  - Login prints only the login response's keys and whether it set a cookie, never the token.
  - Request `/api/v1/daily` with the Bearer header and print the JSON.

  Then verify these against the web app:
  - Which token scheme the server accepts. If it isn't Bearer, switch `fetch_daily` to the working scheme (cookie jar or another header) as design.md describes.
  - That `stats.timeSince` matches the web app's time since the last sitting. If it doesn't, compute the elapsed time from the newest `sittings[].at`.
  - That `stats.sittingDebt` matches what the web app shows as missing.

  Replace the two assumptions in design.md (token scheme, `timeSince` meaning) with what was observed.
- [x] 2.2 Run `uv run sittings --print` against the live API. Verify that it exits 0 and that both lines match the web app at that moment.

## 3. On-device verification (performed by the user on the Pi)

- [ ] 3.1 On the Pi, run `git pull`, create the config file as described in the README (with `chmod 600`), and run `uv run sittings`. Verify that the panel shows the big time and the `missing N` line, that the command exits 0, and that the frame stays on the panel after the prompt returns.
- [ ] 3.2 Add the crontab line from the README with `crontab -e`. Verify that over the next 2–3 minutes the time on the panel updates without any manual run, and that `$HOME/sittings.log` stays empty.
- [ ] 3.3 Temporarily change `url` in the Pi's config file to an unresolvable host, for example `https://sitting.invalid`. Verify that within a minute the panel shows `no network` and `$HOME/sittings.log` gets a line about it. Then restore the URL and verify that the status returns on the next run.

## Workflow follow-up

- Commits and pushes are made by the user (or the git agent on request). The implementation agent cannot run mutating git commands.
- Archive the change after on-device verification passes.

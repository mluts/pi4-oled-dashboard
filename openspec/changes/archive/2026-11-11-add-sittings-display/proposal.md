# Proposal

## Why

The sitting tracker at `https://sitting.lviv.social/` already knows how long it has been since the last sitting and how far behind the day's goal the operator is. Getting that requires opening the web app. The panel sits on the desk all day, so it can show the two numbers directly. This change makes the panel the first real dashboard screen, not just a test pattern.

## What Changes

- Add a `sittings` command (`uv run sittings`). It runs once, then exits; cron runs it every minute. Each run:
  1. Reads the URL, username and password from `sittings.toml` in the repository root, which is gitignored and created by copying the committed `sittings.example.toml`. `--config PATH` overrides the location.
  2. Logs in with `POST /api/v1/login` and fetches today's status with `GET /api/v1/daily`.
  3. Draws one frame on the panel and leaves it there after exiting:
     - A big number: the time since the last sitting, always with a unit letter. Under 1 hour it shows as `45m`, under 3 hours as `2h15`, and from 3 hours on as whole hours rounded down, like `5h`. Before the first sitting of the day, it shows the time since wakeup instead.
     - A smaller line underneath: `missing N`, where N is the server's `sittingDebt`, rounded down and never below 0.
- When no wakeup has been logged today, draw `no wakeup` and exit 0.
- When a run cannot get fresh data, overwrite the panel with a short error (`no network`, `login failed`, `server 502`, `bad response`, `no config`) and exit non-zero. A stale frame never stays on the panel looking current.
- Add a `--print` option. It writes the same text to the terminal instead of the panel and never opens I2C, so the command can be tested on the Mac.
- Add `bin/install-sittings-cron`, which installs (or with `--uninstall`, removes) the every-minute cron entry for this clone. It can be re-run safely and leaves other crontab entries alone.
- README: document the config file, `bin/install-sittings-cron`, and the raw crontab line as a fallback.

## Capabilities

### New Capabilities

- `sitting-status`: shows the operator's sitting status from the sitting tracker API on the panel. Covers credentials, fetching, how the numbers are formatted, empty and error states, the terminal preview, and running unattended from cron.

### Modified Capabilities

None. `oled-display` keeps its `hello` (and pending `splash`) requirements unchanged.

## Impact

- Code: a new module `src/pi_ssd1306_dashboard/sittings.py`. It reuses the DejaVu Sans font that `splash` already bundles in `src/pi_ssd1306_dashboard/fonts/`.
- `pyproject.toml`: a new `sittings` entry under `[project.scripts]`. No new dependencies: HTTP uses `urllib.request` and the config uses `tomllib`, both from the standard library.
- External API: depends on `https://sitting.lviv.social/api/v1/login` and `/api/v1/daily`, as described in `sittings/openapi.json`. The spec does not say how the token is sent. Bearer auth is assumed and will be checked against the live API once it is deployed.
- Repository: a committed `sittings.example.toml`, `/sittings.toml` added to `.gitignore`, and a new `bin/install-sittings-cron` script.
- Pi setup: the operator copies the example to `sittings.toml` (chmod 600), fills it in, then runs `bin/install-sittings-cron`.
- Docs: a new README section.

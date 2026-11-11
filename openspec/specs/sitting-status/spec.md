# sitting-status Specification

## Purpose

Shows the operator's current sitting status from the sitting tracker at `https://sitting.lviv.social/` on the SSD1306 panel: how long since the last sitting and how many sittings are missing today. It runs unattended from cron.

## Requirements

### Requirement: Sittings status command
The project SHALL provide a `sittings` command that logs in to the sitting tracker API with the configured credentials, fetches today's status, draws it on the panel as one frame, and exits. The frame SHALL show the elapsed time as a large number with a smaller `missing N` line below it.

#### Scenario: Status drawn on the Pi
- **WHEN** the operator runs `uv run sittings` on the Pi with the panel connected, a valid config file and the API reachable, and today has a wakeup and at least one sitting
- **THEN** the panel shows the time since the last sitting in large digits
- **AND** a smaller `missing N` line is shown below it
- **AND** the command exits with status 0

### Requirement: Elapsed time format
The elapsed time SHALL always carry a unit letter. Under 1 hour it SHALL be shown as minutes followed by `m`. From 1 hour to under 3 hours it SHALL be shown as hours, `h`, then minutes padded to two digits. From 3 hours on it SHALL be shown as whole hours rounded down, followed by `h`.

#### Scenario: Minutes only
- **WHEN** the elapsed time is 0 h 5 min
- **THEN** it is shown as `5m`

#### Scenario: Just under an hour
- **WHEN** the elapsed time is 0 h 59 min
- **THEN** it is shown as `59m`

#### Scenario: Hours and minutes
- **WHEN** the elapsed time is 1 h 5 min
- **THEN** it is shown as `1h05`

#### Scenario: Just under three hours
- **WHEN** the elapsed time is 2 h 59 min
- **THEN** it is shown as `2h59`

#### Scenario: Exactly three hours
- **WHEN** the elapsed time is 3 h 0 min
- **THEN** it is shown as `3h`

#### Scenario: Hours are rounded down
- **WHEN** the elapsed time is 5 h 47 min
- **THEN** it is shown as `5h`

### Requirement: Missing sittings count
The `missing N` line SHALL show the API's sitting debt rounded down to a whole number, and SHALL show 0 when the debt is negative.

#### Scenario: Fractional debt
- **WHEN** the API reports a sitting debt of 2.6
- **THEN** the line reads `missing 2`

#### Scenario: Partial sitting is not counted
- **WHEN** the API reports a sitting debt of 0.3
- **THEN** the line reads `missing 0`

#### Scenario: Ahead of pace
- **WHEN** the API reports a sitting debt of -0.8
- **THEN** the line reads `missing 0`

### Requirement: Time since wakeup before the first sitting
When today has a wakeup but no sitting yet, the large number SHALL show the time since wakeup, in the same format as the time since the last sitting.

#### Scenario: Morning before any sitting
- **WHEN** today's wakeup was 1 h 40 min ago and no sitting has been logged today
- **THEN** the large number shows `1h40`
- **AND** the `missing N` line is still shown
- **AND** the command exits with status 0

### Requirement: No wakeup logged
When the API reports no wakeup for today, the command SHALL show the text `no wakeup` on the panel instead of the numbers, and exit with status 0.

#### Scenario: Status before wakeup is logged
- **WHEN** the operator has not logged a wakeup today and `uv run sittings` runs
- **THEN** the panel shows `no wakeup`
- **AND** the command exits with status 0

### Requirement: Short error on the panel when status cannot be fetched
When a run cannot get the status, the command SHALL replace the panel content with a short error text, write the error details to standard error, and exit with a non-zero status. The texts SHALL be: `no config`, `no network`, `login failed`, `server <code>` and `bad response`.

#### Scenario: Network unreachable
- **WHEN** the API host cannot be resolved or connected to, or the request times out
- **THEN** the panel shows `no network`
- **AND** the command exits with a non-zero status

#### Scenario: Credentials rejected
- **WHEN** the API answers a request with HTTP 401 or 403
- **THEN** the panel shows `login failed`
- **AND** the command exits with a non-zero status

#### Scenario: Server error
- **WHEN** the API answers with any other non-2xx status, for example 502
- **THEN** the panel shows `server 502`
- **AND** the command exits with a non-zero status

#### Scenario: Unexpected response
- **WHEN** the API answers 2xx but the body is not JSON or lacks the expected fields
- **THEN** the panel shows `bad response`
- **AND** the command exits with a non-zero status

#### Scenario: Previous status is not left behind
- **WHEN** an earlier run drew the status and the current run fails
- **THEN** the earlier numbers are no longer shown on the panel

### Requirement: Credentials from a config file
The command SHALL read the API base URL, username and password from the TOML file `sittings.toml` in the working directory (the repository root). The repository SHALL ignore that file in git and SHALL ship a `sittings.example.toml` with placeholder credentials to copy from. The `--config PATH` option SHALL read them from PATH instead. A missing or unreadable file, or one without all three keys, SHALL be reported as the `no config` error.

#### Scenario: Default config location
- **WHEN** `sittings.toml` in the repository root holds `url`, `username` and `password` and the operator runs `uv run sittings` from the repository root
- **THEN** the command logs in with those credentials

#### Scenario: Credentials stay out of git
- **WHEN** the operator has created `sittings.toml` in the repository root
- **THEN** git treats it as ignored, so it does not show up as a file to commit

#### Scenario: Config created from the example
- **WHEN** the operator copies `sittings.example.toml` to `sittings.toml` and replaces the placeholder username and password
- **THEN** the command accepts the file without further changes

#### Scenario: Config path override
- **WHEN** the operator runs `uv run sittings --config other.toml`
- **THEN** the credentials are read from `other.toml`

#### Scenario: Config file missing
- **WHEN** the config file does not exist
- **THEN** the panel shows `no config`
- **AND** the command exits with a non-zero status

### Requirement: Status persists after exit
The command SHALL leave the drawn frame (status, `no wakeup`, or error) on the panel after it exits, instead of clearing the panel or switching it off.

#### Scenario: Frame remains between cron runs
- **WHEN** `uv run sittings` has exited
- **THEN** the frame it drew is still shown on the panel

### Requirement: Terminal preview
The command SHALL accept a `--print` option that writes the same text it would draw to standard output instead of the panel, with the same exit status. In this mode it SHALL NOT open the I2C bus.

#### Scenario: Preview on the dev machine
- **WHEN** the operator runs `uv run sittings --print` on the macOS dev machine with a valid config file
- **THEN** the elapsed time and the `missing N` line are printed to the terminal
- **AND** the command exits with status 0 without touching I2C

#### Scenario: Error in preview
- **WHEN** the operator runs `uv run sittings --print` and the network is down
- **THEN** `no network` is printed
- **AND** the command exits with a non-zero status

### Requirement: Bounded run time for cron
A single run SHALL finish within 30 seconds even when the API does not respond, so runs started every minute by cron do not overlap.

#### Scenario: Server hangs
- **WHEN** the API accepts the connection but never answers
- **THEN** the command gives up, shows `no network`, and exits with a non-zero status within 30 seconds

### Requirement: Cron installer script
The repository SHALL provide `bin/install-sittings-cron`, which installs a crontab entry that runs `sittings` every minute from the clone the script lives in, using absolute paths. Re-running it SHALL replace its own entry rather than add a second one, and it SHALL leave other crontab entries unchanged. `bin/install-sittings-cron --uninstall` SHALL remove only that entry. The script SHALL refuse to install while `sittings.toml` is missing.

#### Scenario: Fresh install
- **WHEN** the operator runs `bin/install-sittings-cron` on the Pi with `sittings.toml` in place
- **THEN** the crontab contains exactly one entry that runs `sittings` every minute from that clone
- **AND** the script prints the installed entry and exits with status 0

#### Scenario: Re-running does not duplicate
- **WHEN** the operator runs `bin/install-sittings-cron` a second time
- **THEN** the crontab still contains exactly one `sittings` entry

#### Scenario: Other entries are kept
- **WHEN** the crontab already holds unrelated entries and the operator runs `bin/install-sittings-cron` or `bin/install-sittings-cron --uninstall`
- **THEN** the unrelated entries are unchanged

#### Scenario: Uninstall
- **WHEN** the operator runs `bin/install-sittings-cron --uninstall`
- **THEN** the `sittings` entry is removed from the crontab

#### Scenario: Config missing
- **WHEN** `sittings.toml` does not exist in the clone and the operator runs `bin/install-sittings-cron`
- **THEN** the crontab is not changed
- **AND** the script tells the operator to create `sittings.toml` from the example and exits with a non-zero status

### Requirement: Sittings failure when the panel is unreachable
The `sittings` command, when not run with `--print`, SHALL exit with a non-zero status when it cannot open the I2C bus or reach the panel.

#### Scenario: No I2C bus available
- **WHEN** the operator runs `uv run sittings` without `--print` on a machine without `/dev/i2c-1`
- **THEN** the command exits with a non-zero status and reports the error

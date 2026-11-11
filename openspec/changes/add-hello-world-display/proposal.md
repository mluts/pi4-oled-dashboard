# Proposal

## Why

The repository is empty. Before building a dashboard we need proof that the full path works end to end: a uv-managed Python project, deployed to the Raspberry Pi 4B through git, drawing on the SSD1306 OLED via luma.oled over I2C. A "hello world" on the panel is the smallest change that exercises all of it.

## What Changes

- Bootstrap a packaged uv project (`pyproject.toml`, `src/` layout, committed `uv.lock` and `.python-version` pinned to 3.13 to match Pi OS trixie).
- Add `luma.oled` as the only runtime dependency.
- Add a `hello` console script that connects to the SSD1306 (I2C bus 1, address `0x3C`, 128x64), draws the text "hello world", and exits with the text left on the panel (`device.persist = True`).
- Add a README describing the one-time Pi setup (I2C enabled, uv installed, repo cloned) and the dev loop: edit on the Mac, `git push`, then `git pull` and `uv run hello` on the Pi.

## Capabilities

### New Capabilities
- `oled-display`: Connecting to the SSD1306 OLED panel and rendering content on it, starting with a one-shot "hello world" that persists after the process exits.

### Modified Capabilities
<!-- None: no existing specs. -->

## Impact

- **New code**: the whole project skeleton. Nothing exists today beyond OpenSpec scaffolding.
- **Dependencies**: `luma.oled` (pulls in `luma.core`, `pillow`, `smbus2`, `cbor2`). No GPIO or SPI extras are needed for I2C.
- **Target system**: Raspberry Pi 4B, Pi OS trixie, aarch64, system Python 3.13; panel answers at `0x3C` on `/dev/i2c-1` (verified with `i2cdetect`).
- **Dev machine**: macOS. `uv sync` works there for editor support, but `hello` cannot drive hardware on the Mac and is expected to fail with no `/dev/i2c-1`.
- **Out of scope**: dashboard content, refresh loops, systemd service, SIGTERM handling, emulator support, configurable bus/address/size.

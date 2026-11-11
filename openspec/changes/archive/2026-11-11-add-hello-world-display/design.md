# Design

## Context

Greenfield repository. See proposal.md (Why) for the motivation. Constraints that shape the approach:

- Target: Raspberry Pi 4B, Pi OS trixie, aarch64, system Python 3.13. The panel is verified at `0x3C` on bus 1, and I2C is already enabled.
- Dev machine: macOS with no display hardware attached.
- Deployment goes through git: edit on the Mac, `git push`, then `git pull` and `uv run hello` on the Pi.
- luma.core's base dependencies (`pillow`, `smbus2`, `cbor2`) all have macOS and Linux/aarch64 wheels. GPIO and SPI support live behind optional extras that I2C does not need.

## Goals / Non-Goals

**Goals:**
- A project layout the future dashboard can grow into without restructuring.
- One `uv.lock` that resolves identically on the Mac and the Pi.

**Non-Goals:**
- Abstractions for screens, layouts or configuration. Those wait until the dashboard has real content.
- Graceful handling of SIGTERM or other signals.

## Decisions

### Packaged uv project with a `src/` layout
Create it with `uv init --package`. The package name follows the repo (`pi_ssd1306_dashboard` under `src/`), and `[project.scripts]` maps `hello` to a `main()` function.
- *Why*: `uv run hello` gives a stable entry point, and a later systemd unit can call the same script. The `src/` layout stops the code importing from the working tree by accident.
- *Alternative*: a single `hello.py` run with `uv run hello.py`. It's simpler today, but it would have to be restructured once the dashboard needs more than one module.

### Python pinned to 3.13
Set `.python-version` to `3.13` and `requires-python = ">=3.13"`.
- *Why*: uv uses the Pi's system interpreter instead of downloading a standalone build on first run.
- *Alternative*: a lower floor such as `>=3.11`. That adds compatibility nobody needs, since the Pi is the only target.

### Universal lockfile, committed
Commit `uv.lock` and `.python-version`, and add `.venv/` to `.gitignore`.
- *Why*: uv locks are cross-platform by default, so `uv lock` on the Mac produces a lock the Pi can use. `uv run` on the Pi syncs automatically, which satisfies the "runnable from a fresh clone" requirement.

### Hardcoded device parameters
Construct the device inline as `i2c(port=1, address=0x3C)` → `ssd1306(serial)`, using luma's default 128x64 size.
- *Why*: there is one panel, and its values are verified. Making them configurable is listed as out of scope in the proposal.

### Rendering with luma's `canvas` and Pillow's default font
Draw inside `with canvas(device) as draw:`. Use `ImageFont.load_default()` and centre the text using `draw.textbbox`.
- *Why*: the `canvas` context manager handles the image buffer and flushes on exit, which is the idiomatic luma pattern. The default font needs no font files in the repo. Centring based on the text's bounding box keeps the text fully visible whatever font size Pillow picks.
- *Alternative*: building a `PIL.Image` by hand and calling `device.display()`. That's more code for the same result.

### Persistence via `device.persist = True`
Set the flag after drawing, then return normally.
- *Why*: luma registers an atexit hook that calls `device.cleanup()`. With `persist` false, that cleanup sends DISPLAYOFF and clears the RAM. With it true, cleanup only closes the I2C handle, and the SSD1306 keeps refreshing the panel from its own RAM.
- *Alternative*: keeping the process alive with a sleep loop. That was rejected because the hello world is meant to be a one-shot check.

### No custom error handling on the Mac
Let the exception from opening `/dev/i2c-1` propagate. An uncaught exception exits with status 1, which satisfies the failure requirement.

## Risks / Trade-offs

- [Persisted text outlives the OS] On the Pi 4, the 3.3 V rail can stay powered after `shutdown -h`, so the text stays on a halted Pi. → Accepted for hello world. The README notes it, and the dashboard change should revisit it.
- [OLED burn-in] Static text left on the panel for days wears the pixels unevenly. → Acceptable for a test. Run another command or power-cycle to clear it.
- [atexit skipped on SIGTERM/SIGKILL] This doesn't matter for a one-shot command, but it will matter once the dashboard runs as a service. → Deferred to that change.
- [Default font is small] Pillow's default font (about 10 px) is small on a 128x64 panel. → Fine for proving the wiring. Font choice belongs to the dashboard work.

## Migration Plan

There is nothing to migrate. Rollout on the Pi: install uv once (`curl -LsSf https://astral.sh/uv/install.sh | sh`), clone the repo, and run `uv run hello`. The README documents these steps.

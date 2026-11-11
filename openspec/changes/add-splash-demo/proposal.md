# Proposal

## Why

`hello` only shows that the panel works, using one static frame in a Latin bitmap font. This change adds a short demo that exercises animation, frame rate over I2C, a scalable TrueType font and Cyrillic text. It is a repeatable screen test, and it gives the future dashboard a first piece of animation and font code to build on.

## What Changes

- Add a `splash` command (`uv run splash`) that plays a one-shot animation of about 2 seconds:
  1. A filled circle in the center of the panel collapses while 12 dots leave its center and fly outward at the same constant speed until they leave the screen.
  2. The text "Привіт, пацики" grows from a tiny size to a readable size (about 15 px), centered. It starts while the last dots are still on screen.
  3. The final text stays on the panel after the command exits.
- Add a `--gif PATH` option to `splash`. It writes the animation to an animated GIF instead of driving the panel and never opens I2C, so the animation can be previewed and tuned on the Mac.
- Commit the DejaVu Sans TTF and its license inside the package, so Cyrillic renders the same way on every machine without depending on system fonts.
- README: document `uv run splash`, the GIF preview, and an optional tip to raise the I2C bus speed for smoother animation.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `oled-display`: adds requirements for the `splash` demo command, covering the animation sequence, the persisted final text, failure when the panel is unreachable, and the GIF preview. The existing `hello` requirements stay as they are.

## Impact

- Code: new module `src/pi_ssd1306_dashboard/splash.py`, plus a new `fonts/` directory in the package holding `DejaVuSans.ttf` and its license.
- `pyproject.toml`: new `splash` entry under `[project.scripts]`. No new dependencies; Pillow (already pulled in by luma) handles TrueType rendering and GIF writing.
- Docs: README gets a section on `splash`.
- Hardware: none. Optionally, the Pi's I2C bus can be raised to 400 kHz in `/boot/firmware/config.txt`. This is documented but not required.

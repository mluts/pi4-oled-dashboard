# Tasks

## 1. Font asset

- [x] 1.1 Download the DejaVu fonts 2.37 TTF release (`dejavu-fonts-ttf-2.37.tar.bz2` from the `dejavu-fonts/dejavu-fonts` GitHub release `version_2_37`) into the scratchpad. Copy `ttf/DejaVuSans.ttf` and `LICENSE` into `src/pi_ssd1306_dashboard/fonts/`. Verify that both files exist, and that `.venv/bin/python -c` loading the TTF with `ImageFont.truetype` and rendering "Привіт, пацики" produces no `.notdef` boxes: each glyph's mask differs from the mask of an unmapped codepoint such as U+E000.
- [x] 1.2 Measure the ink width of "Привіт, пацики" with the committed font at sizes 12–15 (`font.getbbox`). Verify that 15 px is at most 124 px wide. If it is not, use 14 px as the final size in group 2 and record the measured numbers in design.md under the Text growth decision.

## 2. Splash rendering

- [x] 2.1 Create `src/pi_ssd1306_dashboard/splash.py` with the timeline constants from design.md, font loading via `importlib.resources` (sizes 4 through the final size, loaded once), and a pure `render(t)` that returns a 128x64 mode-"1" image (circle, collapsing radius, 12 dots of 2x2 px, text sized by `t` and centered by its ink bounding box). Verify with `.venv/bin/python -c` that:
  - `render(0)` has white pixels only within radius 12 of (64, 32).
  - At `t = 1.0`, the 12 dot positions are all the same distance from the center.
  - At `t = 1.3`, no dot pixels remain and only text pixels are lit.
  - `render(END)`'s lit bounding box is horizontally centered within ±1 px and does not touch the panel edges.
- [x] 2.2 Add the GIF writer behind `--gif PATH`, as in design.md: `argparse`, `dt = 0.04`, 4x NEAREST upscale, final frame held for 1.5 s, `loop=0`, and no device construction on this path. Verify that `uv run splash --gif` with a scratchpad path exits 0 on the Mac, and that the GIF opens with Pillow at 512x256 with an `n_frames` close to `END / 0.04 + 1`. Open the GIF to review the motion and the overlap of dots and text.
- [x] 2.3 Add the panel loop: open the device as `hello` does, pace frames with `time.monotonic()` toward 30 fps, always display `render(END)` last, then set `device.persist = True`. Register `splash = "pi_ssd1306_dashboard.splash:main"` under `[project.scripts]`. Verify that `uv run splash` with no options on the Mac fails with an error about `/dev/i2c-1` and a non-zero exit status (`echo $status` in fish).
- [x] 2.4 Run `uv build --wheel` into the scratchpad and list the wheel's contents. Verify that `pi_ssd1306_dashboard/fonts/DejaVuSans.ttf` and `LICENSE` are included.
- [x] 2.5 Update `README.md`:
  - The intro line no longer says it only draws "hello world".
  - Add a `uv run splash` section with the GIF preview command.
  - Add an optional note that `dtparam=i2c_arm_baudrate=400000` in `/boot/firmware/config.txt` (followed by a reboot) gives smoother animation.
  - Add a note that the font is DejaVu Sans, with its license in `src/pi_ssd1306_dashboard/fonts/LICENSE`.

  Verify that every new command in the README is copy-pasteable as written.

## 3. On-device verification (performed by the user on the Pi)

- [ ] 3.1 On the Pi, run `git pull` followed by `uv run splash`. Verify that the circle collapses into outward-flying dots, the greeting grows in while the last dots are leaving, every letter of "Привіт, пацики" is legible and nothing is cut off, and the command exits 0.
- [ ] 3.2 After the command has returned to the prompt, verify that the greeting is still shown on the panel.

## Workflow follow-up

- Commits and pushes are made by the user (or the git agent on request). The implementation agent cannot run mutating git commands.
- Optionally, enable 400 kHz I2C on the Pi and re-run `uv run splash` to compare smoothness.
- Archive the change after on-device verification passes.

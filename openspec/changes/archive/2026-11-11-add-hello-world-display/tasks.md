# Tasks

## 1. Project scaffolding

- [x] 1.1 Run `uv init --package --name pi-ssd1306-dashboard --python 3.13` in the repo root. Verify that `pyproject.toml`, `src/pi_ssd1306_dashboard/__init__.py` and `.python-version` (containing `3.13`) exist, and that `requires-python = ">=3.13"`.
- [x] 1.2 Make sure `.gitignore` covers `.venv/`, `__pycache__/` and `*.pyc`. Verify that `llm-git check-ignore .venv` prints `.venv`.
- [x] 1.3 Run `uv add luma.oled`. Verify that `luma-oled` appears under `[project].dependencies`, that `uv.lock` exists, and that `uv sync` succeeds on the Mac.
- [x] 1.4 Verify that the lock is universal: `uv lock --check` passes, and `uv.lock` has Pillow wheel entries for both `macosx` and `manylinux*_aarch64`.

## 2. Hello world command

- [x] 2.1 Implement `main()` in the package as described in design.md: build `i2c(port=1, address=0x3C)` and `ssd1306(serial)`, draw "hello world" centred with the default font inside `canvas(device)`, and set `device.persist = True`. Verify that `uv run python -c "import pi_ssd1306_dashboard"` succeeds on the Mac.
- [x] 2.2 Add `hello = "pi_ssd1306_dashboard:main"` (or the module path the scaffold produced) under `[project.scripts]`. Verify that `uv run hello` on the Mac fails with an error about `/dev/i2c-1` and a non-zero exit status (`echo $status` in fish).
- [x] 2.3 Write `README.md` with the hardware (Pi 4B, SSD1306 128x64 at `0x3C`, I2C bus 1), the one-time Pi setup (enable I2C, check with `i2cdetect -y 1`, install uv, clone), the dev loop (edit on the Mac → `git push` → `git pull` + `uv run hello` on the Pi), and a note that the persisted text stays until power-off or the next draw. Verify that every command in the README is copy-pasteable as written.

## 3. On-device verification (performed by the user on the Pi)

- [x] 3.1 Push the branch, then on the Pi run `git clone` (or `git pull`) followed by `uv run hello`. Verify that the command exits 0 and that "hello world" is fully visible and centred on the panel.
- [x] 3.2 After the command from 3.1 has returned to the shell prompt, look at the panel. Verify that "hello world" is still shown, which checks the persistence requirement.

## Workflow follow-up

- Commits and pushes are made by the user (or the git agent on request). The implementation agent cannot run mutating git commands.
- Archive the change after on-device verification passes.

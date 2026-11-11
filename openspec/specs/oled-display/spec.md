# oled-display Specification

## Purpose

Drives the SSD1306 OLED panel attached to the Raspberry Pi over I2C: connecting to it and rendering content on it for the operator to read.

## Requirements

### Requirement: Hello world command
The project SHALL provide a `hello` command that connects to the SSD1306 panel (128x64, I2C bus 1, address `0x3C`) and draws the text "hello world" so it is fully visible on the panel.

#### Scenario: Draw hello world on the Pi
- **WHEN** the operator runs `uv run hello` on the Raspberry Pi with the panel connected
- **THEN** the panel shows the text "hello world" with no part of it cut off
- **AND** the command exits with status 0

### Requirement: Rendered content persists after exit
The `hello` command SHALL leave the drawn content on the panel after the process exits, instead of clearing the panel or switching it off.

#### Scenario: Text remains after the command returns
- **WHEN** `uv run hello` has completed successfully
- **THEN** the text "hello world" is still shown on the panel

### Requirement: Failure when the panel is unreachable
The `hello` command SHALL exit with a non-zero status when it cannot open the I2C bus or reach the panel.

#### Scenario: No I2C bus available
- **WHEN** the operator runs `uv run hello` on a machine without `/dev/i2c-1` (for example the macOS dev machine)
- **THEN** the command exits with a non-zero status and reports the error

### Requirement: Runnable from a fresh clone
The `hello` command SHALL run on the Pi from a fresh clone or after `git pull` using only `uv run hello`, with no separate manual install step.

#### Scenario: First run after cloning
- **WHEN** the operator clones the repository on the Pi (with uv installed and I2C enabled) and runs `uv run hello`
- **THEN** uv installs the locked dependencies and the panel shows "hello world"

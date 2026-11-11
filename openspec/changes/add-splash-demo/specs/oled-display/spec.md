# Spec Delta

## ADDED Requirements

### Requirement: Splash burst animation
The project SHALL provide a `splash` command that, on the SSD1306 panel, first shows a filled circle centered on the panel. The circle then shrinks to nothing while a set of dots leaves its center in evenly spaced directions. Every dot moves at the same constant speed and keeps going until it is off the panel.

#### Scenario: Burst plays on the Pi
- **WHEN** the operator runs `uv run splash` on the Raspberry Pi with the panel connected
- **THEN** the panel shows a filled circle in its center
- **AND** the circle shrinks away while dots fly outward from the center in all directions
- **AND** no dots remain on the panel once the burst is over

#### Scenario: Dots move at equal constant speed
- **WHEN** the burst animation is playing
- **THEN** all dots are the same distance from the panel center at any moment, until they leave the panel
- **AND** that distance grows by the same amount per unit of time throughout the burst

### Requirement: Splash greeting text grows in
After the burst, the `splash` command SHALL show the text "Привіт, пацики" centered on the panel, growing in steps from a tiny size to a final size of 12–15 px. The text SHALL start growing before the last dots have left the panel.

#### Scenario: Text grows to readable size
- **WHEN** the burst animation nears its end
- **THEN** the text "Привіт, пацики" appears centered on the panel at a tiny size while some dots are still visible
- **AND** it grows until it reaches its final size of 12–15 px

#### Scenario: Final text is fully visible and correct
- **WHEN** the text has reached its final size
- **THEN** every character of "Привіт, пацики", including the Cyrillic letters and the comma, is rendered as a real glyph (no placeholder boxes)
- **AND** no part of the text is cut off by the panel edges

### Requirement: Splash renders text without system fonts
The `splash` command SHALL render the Cyrillic greeting using only files shipped in the repository, so it does not depend on fonts installed on the machine.

#### Scenario: Fresh Pi without extra font packages
- **WHEN** the operator clones the repository on a Pi that has no extra font packages installed and runs `uv run splash`
- **THEN** the greeting renders with correct Cyrillic glyphs

### Requirement: Splash final frame persists after exit
The `splash` command SHALL exit when the animation finishes and leave the final greeting text on the panel, instead of clearing the panel or switching it off.

#### Scenario: Greeting remains after the command returns
- **WHEN** `uv run splash` has completed successfully
- **THEN** the command exits with status 0
- **AND** the panel still shows "Привіт, пацики" at its final size

### Requirement: Splash failure when the panel is unreachable
The `splash` command, when not writing a GIF preview, SHALL exit with a non-zero status when it cannot open the I2C bus or reach the panel.

#### Scenario: No I2C bus available
- **WHEN** the operator runs `uv run splash` with no options on a machine without `/dev/i2c-1` (for example the macOS dev machine)
- **THEN** the command exits with a non-zero status and reports the error

### Requirement: Splash GIF preview
The `splash` command SHALL accept a `--gif PATH` option that writes the full animation, including the held final frame, to an animated GIF at PATH and exits. In this mode the command SHALL NOT open the I2C bus. The GIF's timing SHALL match the timing of the animation on the panel.

#### Scenario: Preview on the dev machine
- **WHEN** the operator runs `uv run splash --gif splash.gif` on the macOS dev machine
- **THEN** the command exits with status 0 without touching I2C
- **AND** `splash.gif` plays the burst followed by the growing greeting
- **AND** the GIF ends on the final greeting frame

#### Scenario: Preview is legible at desktop size
- **WHEN** the operator opens the generated GIF in an image viewer
- **THEN** each panel pixel is shown as a square block larger than one screen pixel, so the 128x64 frame is easy to inspect

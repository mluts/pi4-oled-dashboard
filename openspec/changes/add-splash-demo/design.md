# Design

## Context

See proposal.md for the motivation. These are the facts that shape the approach:

- `hello` (`src/pi_ssd1306_dashboard/__init__.py`) draws a single frame with `canvas(device)` and sets `device.persist = True`. It is the only existing code.
- Installed stack: Pillow 13.0, luma.oled 3.16, luma.core 2.6. Pillow's built-in fonts cover only Latin text: the legacy bitmap font is Latin-1, and the bundled Aileron used by `load_default(size=...)` has no Cyrillic. A TrueType font with Cyrillic coverage has to be supplied.
- I2C at the Pi's default 100 kHz moves a full 1024-byte SSD1306 frame in roughly 90–100 ms, which is about 10 fps. luma's default framebuffer sends only the bounding box that changed. That does not help much during the burst, because the dots span the whole panel.
- Width measured with Verdana and Arial, which are about as wide as DejaVu: "Привіт, пацики" is about 118 px wide at 15 px Verdana and about 94 px at 12 px. It fits on one line of the 128 px panel.
- The dev machine (macOS) has no panel, so tuning the animation there needs an off-device output.

## Goals / Non-Goals

**Goals:**
- The animation timing is defined in seconds, so the panel and the GIF play the same choreography whatever the actual frame rate is.
- The GIF preview and the panel use the same drawing code, so the preview cannot drift from what the Pi shows.

**Non-Goals:**
- A reusable animation or scene framework for the dashboard. Everything lives in one module, and nothing is extracted until a second animation exists.
- Configurable text, timing or dot count from the command line. These are module constants.
- Changing `hello`, which stays on the default font.
- Changing the Pi's I2C bus speed. That is documented as an optional tip only.

## Decisions

### `render(t)` as a pure function of time
`splash.py` defines a function that takes `t` in seconds and returns a fresh 128x64 mode-"1" `PIL.Image`. It holds no state and does no I/O. Two outputs consume it:

```
render(t) --+--> panel loop: t from the monotonic clock, device.display(img)
            +--> GIF writer: t = 0, dt, 2dt, ..., upscale, save
```

- *Why*: the burst's geometry is a closed-form function of `t` (radius, dot distance), and so is the text size. Driving everything by `t` makes a slow I2C bus drop frames instead of slowing the animation, and it makes the GIF a faithful preview.
- *Alternative*: a frame counter with a fixed step per frame. That is simpler, but on a 10 fps bus the animation would run three times slower than in the preview.
- *Alternative*: using luma's `canvas` per frame, as `hello` does. That ties drawing to a device, so the GIF path would need a fake device. Building the image directly and calling `device.display(img)` keeps `render` device-free.

### Timeline

All times are module constants in seconds, so they are easy to retune after watching the GIF.

```
0.0 -- 0.3  full circle, radius R = 12, centered at (64, 32)
0.3 -- 0.7  radius shrinks linearly from R to 0
0.3 -- 1.3  12 dots at angles k*30 deg, distance from center = V * (t - 0.3)
            V = 75 px/s, so at t = 1.3 the distance is 75 px, past the
            farthest corner (about 72 px), and every dot is off the panel
1.1 -- 2.0  text grows through integer font sizes 4..15
            (12 steps, each held for an equal share of the window)
2.0         final frame: text at 15 px; animation ends
```

- Dots are 2x2 px squares, centered on their position. A single pixel is easy to lose on a 0.96" panel.
- Dots start at the center at the same moment the circle starts to shrink. They are white on the white disc, so they show up as they pass the shrinking edge, which reads as the circle breaking apart. No special-casing is needed.
- The text starts at 1.1 s, when the dots are about 60 px out. Horizontal and diagonal dots are still on screen at that point, which gives the requested overlap.

### Text growth by re-rendering each integer size
Each frame chooses an integer size from `t`. The text is drawn with that size's `FreeTypeFont` and centered by its ink bounding box (`draw.textbbox`), the same centering approach as `hello`. Fonts for sizes 4–15 are loaded once, at startup.

- *Why*: on a 1-bit panel, real glyphs rendered at each size stay crisp from about 8 px up. Scaling one large bitmap down would turn into mush once it is thresholded to 1-bit.
- *Trade-off*: the size changes in steps, and the width "breathes" a little because hinting does not scale linearly. That is acceptable for a short grow-in.
- *Final size*: 15 px, provided the rendered width is at most 124 px (at least 2 px margin on each side). If DejaVu turns out wider, the final size drops to 14. A task checks this.

### DejaVu Sans committed inside the package
Store `src/pi_ssd1306_dashboard/fonts/DejaVuSans.ttf` together with the DejaVu `LICENSE`. Locate it with `importlib.resources.files("pi_ssd1306_dashboard") / "fonts" / "DejaVuSans.ttf"`.

- *Why*: Cyrillic coverage (including Ukrainian і), a permissive license that allows redistribution as long as the notice is kept, and identical rendering on the Mac and the Pi. Keeping the font inside the package, rather than in the repo root, means `uv_build` ships it in the wheel and `importlib.resources` finds it in both editable and installed environments.
- *Source*: DejaVu fonts 2.37 release tarball (`dejavu-fonts-ttf-2.37`), which contains both `ttf/DejaVuSans.ttf` and `LICENSE`. Copying `/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf` from the Pi yields the same file, but the license must still come from upstream.
- *Alternative*: depending on the system font path on the Pi. That breaks the GIF preview on the Mac and ties the demo to an apt package.
- *Alternative*: a pixel font designed for small sizes. That would be crisper at 6–8 px, but the scaling step needs a scalable outline font anyway.

### Panel loop paced by the monotonic clock
1. Open the device the same way `hello` does.
2. Record `start = time.monotonic()`.
3. Loop:
   - Compute `t = min(now - start, END)`.
   - Call `device.display(render(t))`.
   - Stop once `t == END`.
   - Otherwise sleep until the next 1/30 s tick. If a frame took longer than a tick, skip the sleep.
4. Set `device.persist = True` and return.

- *Why*: the final frame is always exactly `render(END)`, whatever frames were dropped along the way. Persistence works exactly as it does for `hello`.

### GIF writer
- Sample `render` at `dt = 0.04` s (40 ms). GIF frame delays are stored in hundredths of a second, so 40 ms is exact, while 1/30 s is not.
- Upscale each frame 4x with `Image.NEAREST` (512x256), so panel pixels show as square blocks.
- Save with `frames[0].save(path, save_all=True, append_images=frames[1:], duration=durations, loop=0)`.
- Hold the last frame for 1.5 s before the GIF loops, so the final state is easy to inspect.
- The device is never constructed on this path, so the Mac does not need I2C.

### CLI via `argparse` with a single optional `--gif PATH`
- *Why*: it is in the standard library and enough for one flag. With no options, the command drives the panel, which mirrors `hello`.

### No custom error handling on the device path
As with `hello`, the exception from opening `/dev/i2c-1` propagates and exits with status 1.

## Risks / Trade-offs

- [Low frame rate at 100 kHz] The burst may look jerky on the Pi (about 10 fps). → Time-based rendering keeps the duration correct. The README documents `dtparam=i2c_arm_baudrate=400000` in `/boot/firmware/config.txt` as an optional improvement.
- [DejaVu is wider than measured] The 15 px text could exceed 124 px. → A task measures the real width with the committed font, and the final size falls back to 14 px.
- [Text sizes below about 7 px are unreadable blobs] → Intended: these steps read as "emerging", and they last only about 0.3 s.
- [Font file not packaged] If `uv_build` left out the TTF, an installed wheel would fail to find it. → A task builds the wheel and checks that the font is listed. `uv run` uses an editable install from `src/`, which is unaffected either way.
- [Repository size] The TTF adds about 750 KB. → Acceptable for a single committed asset.
- [OLED burn-in] Same as `hello`: the greeting persists until the next draw or power-off. → Acceptable for a demo.

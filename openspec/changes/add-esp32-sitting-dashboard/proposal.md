# Proposal

## Why

The sitting status currently needs a Raspberry Pi, cron and a Python environment to drive one small panel. A Waveshare ESP32-C6-Zero with an SSD1315 panel and four buttons can show the same two numbers on its own: it costs a few dollars, draws almost no power, and needs only Wi-Fi. The `esp32-ssd1315-dashboard/` ESP-IDF project exists but is empty; this change gives it its first firmware.

## What Changes

- Firmware for the ESP32-C6 in `esp32-ssd1315-dashboard/` that, in one long-running loop:
  1. Initialises the panel first and keeps something on it at every stage: `booting`, then the Wi-Fi SSID it is connecting to (with the attempt count on retries), then `fetching`, then the status.
  2. Connects to a Wi-Fi network as a station and reconnects whenever the link drops.
  3. Every 30 s (configurable) fetches `GET /api/v1/daily` from the sitting tracker with a pre-issued bearer token, over HTTP or HTTPS, and draws the same screen as the Pi's `sittings` command: the time since the last sitting as a big number with a unit letter, `missing N` below it, `no wakeup` when no wakeup is logged. Only the `stats` object of the response is used.
  4. Keeps the numbers on the panel until a fetch produces a result, so a scheduled refresh never flickers. A failed fetch replaces them with a short error: `no network`, `bad token`, `server <code>`, `bad response`, `no config`. A Wi-Fi drop replaces them with `no wifi` at once, and a reconnect fetches immediately.
- Four buttons on configurable GPIOs. One is the refresh button: a first press shows `refresh?` in the top-right corner in the smallest font, a second press within a 1 s window (configurable) fetches now, otherwise the prompt disappears. The other three are initialised as inputs and do nothing yet.
- Everything an operator may need to change is a `menuconfig` option: I2C SDA/SCL pins, panel address and bus frequency; the four button GPIOs; URL scheme, host and port; Wi-Fi SSID and passphrase; API token; refresh interval; refresh-confirm window.
- Project settings: 4 MB flash with the large single-app partition table, committed as `sdkconfig.defaults`; the generated `sdkconfig` becomes gitignored because the Wi-Fi passphrase and the token end up in it.
- README: a new section on building, configuring and flashing the ESP32 variant, including how to obtain the token.

Not in this change: logging in with username/password on the device (the login endpoint is still changing; the token is obtained out of band), functions for the three spare buttons, pixel-identical fonts to the Pi, runtime provisioning without reflashing, OTA updates.

## Capabilities

### New Capabilities

- `esp32-sitting-dashboard`: the standalone ESP32 firmware that shows the sitting status on the SSD1315 panel. Covers build-time configuration, boot and Wi-Fi progress on the panel, periodic and button-triggered refresh, the error states, and the button inputs. It reuses the number formatting rules of `sitting-status` (elapsed time format, missing count, wakeup fallback) by reference rather than redefining them.

### Modified Capabilities

None. `sitting-status` and `oled-display` keep describing the Pi commands unchanged.

## Impact

- Code: new sources under `esp32-ssd1315-dashboard/main/` (replacing the empty `app_main`), a `Kconfig.projbuild` for the options, an `idf_component.yml` with the dependencies, and `sdkconfig.defaults` in the project root.
- Dependencies, via the IDF component manager: `u8g2` (panel driver and fonts), `espressif/cjson` (cJSON moved out of IDF core in 6.x), `espressif/button` (debounced GPIO buttons). All are fetched at build time into `managed_components/`.
- ESP-IDF 6.1 components already in the build: `esp_wifi`, `esp_netif`, `nvs_flash` (Wi-Fi calibration data), `esp_http_client`, `esp-tls` with the bundled certificate store, `esp_driver_i2c`, `esp_timer`.
- External API: `GET /api/v1/daily` with `Authorization: Bearer <token>`, as `sittings/openapi.json` describes. The device never calls `/api/v1/login`.
- Repository: `.gitignore` gains `esp32-ssd1315-dashboard/sdkconfig` and `esp32-ssd1315-dashboard/managed_components/`; the currently staged `sdkconfig` has to be unstaged by the operator (`git rm --cached`).
- Hardware: Waveshare ESP32-C6-Zero, SSD1315 128x64 over I2C, four momentary buttons to ground using the internal pull-ups.
- Docs: a new README section for the ESP32 variant.

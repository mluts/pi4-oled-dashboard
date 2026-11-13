# Design

## Context

See proposal.md for the motivation and specs/esp32-sitting-dashboard/spec.md for the behaviour. The facts that shape the approach:

- `esp32-ssd1315-dashboard/` is a freshly generated ESP-IDF 6.1 project: target `esp32c6`, one empty `app_main` in `main/esp32-ssd1315-dashboard.cpp`, C++ exceptions and RTTI off, `sdkconfig` staged in git. Its `sdkconfig` still has the 2 MB flash / 1 MB single-app defaults; the C6-Zero has 4 MB.
- IDF 6.1 ships an `esp_lcd` SSD1306 panel driver but nothing that draws text. Its `examples/peripherals/i2c/i2c_u8g2` wires u8g2 to the new `esp_driver_i2c` master API with two callbacks; that is the pattern to copy. The SSD1315 is register-compatible with the SSD1306, so the SSD1306 setup applies.
- cJSON is not in the IDF 6 core components; it is `espressif/cjson` on the component registry.
- `CONFIG_MBEDTLS_HAVE_TIME_DATE` is off in the generated `sdkconfig`, so certificate validity dates are not checked and the device needs no SNTP to open TLS.
- The Pi reference, `src/pi_ssd1306_dashboard/sittings.py`, defines the screen rules (`screen_for`, `format_elapsed`) and the layout (17 px label and bottom line, 30 px big line, 15 px messages, DejaVu Sans). The `sitting-status` spec is the normative source for the formatting rules.
- The API's `/daily` response carries the full day's `sittings` array; 100 entries at roughly 75 bytes each plus `stats`, `settings` and `wakeup` is about 8 KB.
- Waveshare ESP32-C6-Zero pins, from its wiki: GPIO 0–7, 14, 15 and 18–23 are on the headers; GPIO 8 drives the on-board WS2812, GPIO 9 is the BOOT button, GPIO 12/13 are USB D-/D+. GPIO 4, 5, 8, 9 and 15 are strapping pins. Verify against the board before wiring; the defaults below avoid the strapping and USB pins.
- The operator wants app code free of dynamic allocation after boot; libraries may allocate internally.

## Goals / Non-Goals

**Goals:**
- One event-driven main loop that owns the panel; every state the device can be in has a defined screen.
- The decision logic (JSON → screen, elapsed formatting) is plain C with no IDF headers, so it can be compiled and checked on the Mac with the system compiler.
- All app buffers static and sized once; no `malloc` in app code outside initialisation.
- Minimal dependency surface: three managed components, otherwise IDF built-ins.

**Non-Goals:**
- Pixel parity with the Pi's DejaVu rendering. u8g2 fonts of similar size are used; a custom converted font can come later.
- Power management, deep sleep, burn-in mitigation.
- Any write to the API; login on the device.
- Host-side unit test infrastructure beyond compiling the pure module with `cc` from the scratchpad.

## Decisions

### Modules

```
main/
  esp32-ssd1315-dashboard.cpp  app_main: init order, event loop, state machine
  status.c / status.h          pure C: daily JSON (cJSON) -> screen_t, format_elapsed
  screen.c / screen.h          u8g2 setup on esp_driver_i2c; draw(screen_t, prompt)
  net.c / net.h                wifi station + reconnect, posts WIFI_UP/WIFI_DOWN
  fetch.c / fetch.h            esp_http_client GET -> static body buffer -> status.c
  buttons.c / buttons.h        espressif/button handles, posts BTN_REFRESH
  Kconfig.projbuild            the menuconfig options
  idf_component.yml            u8g2, espressif/cjson, espressif/button
```

`status.c` is the only module with behaviour worth checking off-device; it includes `cjson.h` and the C standard library and nothing else, so `cc -I managed_components/espressif__cjson status.c cJSON.c test.c` builds on the Mac.

### One main task, one event queue

```
 esp_timer (period)  --TICK------------+
 button cb           --BTN_REFRESH-----+       +--------------------+
 esp_timer (prompt)  --PROMPT_TIMEOUT--+-----> | main task          | ---> screen_draw()
 wifi event handler  --WIFI_UP/DOWN----+       | state + model      |
 fetch task          --FETCH_DONE(res)-+       +---------+----------+
                                                         | FETCH_NOW (task notify)
                                                         v
                                               +--------------------+
                                               | fetch task         |
                                               | http -> parse      |
                                               +--------------------+
```

- The main task is the only writer of the display and the only owner of the model (`screen_t current`, `bool prompt`, `wifi_state`). No mutex around u8g2.
- The fetch runs in its own task so a 10-second request never blocks button handling or the Wi-Fi-down redraw. It idles on `ulTaskNotifyTake`; the main task notifies it for a scheduled tick, a confirmed refresh, or WIFI_UP. If a notification arrives while a fetch is running, FreeRTOS keeps it pending and the task fetches once more right after; fetches cannot overlap by construction.
- Queue, task stacks and TCBs are created with the `*Static` FreeRTOS APIs; the two `esp_timer`s and the HTTP client handle are created once in `app_main`. After that the app code performs no allocation. Wi-Fi, lwIP, mbedTLS, cJSON and the button component allocate internally as they see fit.
- *Alternative*: do the fetch inline in the main task. Simpler, but `refresh?` and `no wifi` would wait for a hung request to time out.

### State machine of the main task

```
  BOOT --(panel up)--> CONNECTING --(WIFI_UP)--> ONLINE --(WIFI_DOWN)--> OFFLINE
                           ^                       |  ^                     |
                           |                   TICK/BTN/FETCH_DONE          |
                           +---------------------------(WIFI_UP, fetch now)-+
```

| State       | Screen                                        | On event |
|-------------|-----------------------------------------------|----------|
| BOOT        | `booting`                                     | leaves as soon as the stack is up |
| CONNECTING  | SSID, plus `try N` from the second attempt     | WIFI_UP → ONLINE, show `fetching`, notify fetch |
| ONLINE      | status, message or error from the last fetch  | TICK → notify fetch; FETCH_DONE → redraw; WIFI_DOWN → OFFLINE |
| OFFLINE     | `no wifi`                                     | WIFI_UP → ONLINE, keep `no wifi` until FETCH_DONE, notify fetch |
| any but BOOT| `refresh?` overlay while the prompt is armed  | BTN_REFRESH: arm + start window, or confirm; PROMPT_TIMEOUT: disarm |

Screens are redrawn from the model (`current` + `prompt`), never patched, so the prompt overlay and its removal cannot leave artefacts. A confirmed refresh while OFFLINE only clears the prompt; the reconnect already triggers a fetch.

`no config` (empty SSID or token) is decided in `app_main` after the panel is up: the screen is drawn and the task parks forever; Wi-Fi is never started.

### Panel: u8g2 over `esp_driver_i2c`

- `u8g2_Setup_ssd1306_i2c_128x64_noname_f` with the byte callback and GPIO/delay callback from the IDF `i2c_u8g2` example (bus created with `i2c_new_master_bus`, device added in `U8X8_MSG_BYTE_INIT`, internal pull-ups on). The full-frame buffer lives inside the static `u8g2_t`.
- Fonts, chosen to match the Pi's proportions on 64 rows: `u8g2_font_helvR14_tr` for `since` and `missing N` (the Pi uses 17 px DejaVu), `u8g2_font_logisoso30_tr` for the big line (30 px, includes `h` and `m`), `u8g2_font_helvR14_tr` for centred messages (`no wakeup`, errors, boot texts), `u8g2_font_5x7_tr` for `refresh?`. The big line is centred with `u8g2_GetStrWidth`; the vertical band is what remains between the label's baseline and the bottom line's ascent, as on the Pi. If `2h59` turns out wider than 124 px or clips vertically at implementation time, step down one logisoso size and record it here.
- Boot screens with two lines (SSID over `try N`) use the message font; an SSID longer than the panel is cut at the width with `u8g2_DrawStr`'s natural clipping, nothing is wrapped.
- *Alternative*: `esp_lcd` + LVGL, as in IDF's `i2c_oled` example. It adds a render task, a tick, and 200 KB of library to draw three strings.

### Wi-Fi

- Standard station bring-up: `nvs_flash_init` (the PHY needs it), `esp_netif_init`, default event loop, `esp_netif_create_default_wifi_sta`, `esp_wifi_init/set_mode/set_config/start`. The event handler calls `esp_wifi_connect` on `WIFI_EVENT_STA_START` and again on every `WIFI_EVENT_STA_DISCONNECTED`, increments an attempt counter and posts `WIFI_DOWN` (with the count); `IP_EVENT_STA_GOT_IP` resets the counter and posts `WIFI_UP`.
- No retry limit and no backoff: the driver already paces reconnects, and the operator wants the board to recover by itself.
- The Wi-Fi config struct is filled from `CONFIG_DASH_WIFI_SSID` / `CONFIG_DASH_WIFI_PASSWORD`; `sta.threshold.authmode` is left at the IDF default so open, WPA2 and WPA3 networks all work.

### HTTP fetch

- URL built once at boot into a static buffer: `"%s://%s:%d/api/v1/daily"`. `esp_http_client_init` once with `timeout_ms = 10000`, `crt_bundle_attach = esp_crt_bundle_attach` (ignored for `http`), an event handler and `user_data` pointing at the body accumulator. `Authorization: Bearer <token>` is set with `esp_http_client_set_header` at init; the handle is reused for every `esp_http_client_perform`.
- Body accumulator: a static 16 KB buffer plus a length. `HTTP_EVENT_ON_DATA` appends; an overflow sets a flag that maps to `bad response` after `perform` returns. 16 KB covers 100 sittings twice over.
- Timing: the 10 s client timeout applies to connect, TLS and each read, so a server that accepts and hangs fails within 10 s. The spec's 15 s bound leaves room for DNS. The fetch task also resets the task watchdog subscription it holds around each `perform`.
- Error mapping, mirroring the Pi's:

| `esp_http_client_perform` outcome                                  | Panel text      |
|--------------------------------------------------------------------|-----------------|
| not `ESP_OK` (connect, DNS, TLS, timeout)                          | `no network`    |
| status 401 or 403                                                  | `bad token`     |
| any other status outside 200–299                                   | `server <code>` |
| overflowed body, `cJSON_Parse` fails, `stats` missing or mistyped  | `bad response`  |

The detail logged with `ESP_LOGW` is the `esp_err_t` name or the status code, never the header or the body.

### JSON to screen (`status.c`)

Same rules as `sittings.py`, in C:

1. `cJSON_ParseWithLength(body, len)`; failure → `bad response`.
2. `stats` absent or not object/null → `bad response`; `stats` is JSON null → message `no wakeup`.
3. `timeSince` is an array of two numbers → `elapsed = h*60 + m`; null → `elapsed = fromWakeupMins` (must be a number); anything else → `bad response`.
4. `sittingDebt` must be a number; `missing = max(0, floor(debt))`; `elapsed = max(0, elapsed)`.
5. `format_elapsed(minutes)`: `<60` → `%dm`, `<180` → `%dh%02d`, else `%dh`.
6. `cJSON_Delete` before returning. The tree is transient inside one fetch, so cJSON's heap use leaves nothing behind. (A static arena through `cJSON_InitHooks` was considered and dropped: the operator's rule is "libraries may allocate", and the arena would only add a failure mode.)

`screen_t` is a small struct: `kind` (STATUS / MESSAGE), `elapsed[8]`, `missing`, `text[16]`. The main task copies it into its model; nothing points into the parsed tree.

### Buttons

- `espressif/button`: one `iot_button_new_gpio_device` per configured GPIO, active low with the internal pull-up. The refresh button registers `BUTTON_PRESS_DOWN`; the callback posts `BTN_REFRESH` to the queue (the callback runs in the component's own task, so it may block briefly on the queue). The three spare buttons are created with no callbacks, which still configures and debounces the GPIO.
- The confirm window is a one-shot `esp_timer` started on the first press; its callback posts `PROMPT_TIMEOUT`. The component's own double-click event is not used because it reports only after the second press and could not show the prompt.
- Defaults: refresh on GPIO 0, spares on GPIO 1, 2, 3; SDA GPIO 6, SCL GPIO 7, 400 kHz, address 0x3C. All are `menuconfig` options.

### Kconfig

`main/Kconfig.projbuild`, menu "Sitting dashboard", options prefixed `DASH_`: `I2C_SDA`, `I2C_SCL`, `I2C_FREQ_HZ`, `I2C_ADDRESS` (hex), `BTN_REFRESH_GPIO`, `BTN_SPARE1_GPIO`, `BTN_SPARE2_GPIO`, `BTN_SPARE3_GPIO`, `API_SCHEME` (choice http/https), `API_HOST`, `API_PORT`, `API_TOKEN`, `WIFI_SSID`, `WIFI_PASSWORD`, `REFRESH_SECONDS` (range 5–3600), `CONFIRM_MS` (range 200–5000). GPIO options use the `ENV_GPIO_RANGE_*` bounds via the same `orsource` line as the IDF example.

### `sdkconfig.defaults` and git

Committed `sdkconfig.defaults`: `CONFIG_IDF_TARGET="esp32c6"`, `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y`, `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y`, and nothing secret. The pin and host defaults live in the Kconfig `default` lines, not here, so `menuconfig` shows them as defaults rather than as overrides. `.gitignore` gains `esp32-ssd1315-dashboard/sdkconfig` (it will hold the passphrase and the token) and `esp32-ssd1315-dashboard/managed_components/`; `dependencies.lock` is committed so builds are reproducible. The already staged `sdkconfig` has to be removed from the index by the operator; the implementation agent cannot run git.

*Alternative*: keep `sdkconfig` committed and put the secrets in a separate `sdkconfig.secrets` listed in `SDKCONFIG_DEFAULTS`. That splits one `menuconfig` session across two files and is easy to get wrong; a single ignored `sdkconfig` is the common IDF practice.

### C++ entry point

`app_main` stays in the existing `.cpp` so the project builds as generated; the other modules are C with `extern "C"` headers. Nothing in the app uses exceptions or RTTI.

## Risks / Trade-offs

- [Flash] Wi-Fi + TLS + certificate bundle + u8g2 fonts may approach the 1.5 MB large app partition. → Check `idf.py size` after the first full build; `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN` (common CAs only) and dropping unused u8g2 fonts (only the four named ones are linked) are the levers.
- [TLS date check] If `CONFIG_MBEDTLS_HAVE_TIME_DATE` is ever enabled, TLS fails until the clock is set. → A NOTE in `sdkconfig.defaults` and the README; SNTP is a later change if wanted.
- [Response growth] A day with more than ~200 sittings would overflow the 16 KB buffer and show `bad response`. → Out of range for a real day; the buffer size is one constant.
- [Token lifetime] If the server expires tokens, the panel shows `bad token` until the operator issues a new one and reflashes. → Accepted for now; login on the device is deferred until the endpoint stabilises.
- [Heap over weeks] TLS handshakes every 30 s churn the heap. → Minimum free heap is logged after each fetch; the reused HTTP handle keeps lwIP/TLS allocations bounded. If fragmentation shows up, keep-alive or a longer interval are the knobs.
- [Button task blocking] The button callback posts to the queue from the component's task; if the queue were full it would block that task. → Queue depth 8 with the main task draining promptly; a post uses a zero timeout and drops the press with a log line rather than blocking.
- [Pin defaults] The C6-Zero pin facts come from the vendor wiki, not from the board in hand. → Defaults are `menuconfig` options; the README tells the operator to check them.

## Migration Plan

Additive. Deploy: `idf.py set-target esp32c6` (once), `idf.py menuconfig` to enter SSID, passphrase, token and any pin changes, `idf.py -p <port> flash monitor`. The Pi setup is untouched and keeps working in parallel. Rollback: flash any earlier firmware or erase the flash; nothing outside the board changes.

## Open Questions

- Which u8g2 font sizes make the layout look closest to the Pi on the real SSD1315 is a judgement call to make with the panel in hand; the candidates above are the starting point and the task that places the text records the final choice here.

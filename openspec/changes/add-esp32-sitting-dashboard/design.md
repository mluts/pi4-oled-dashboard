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
- The decision logic (JSON → screen, elapsed formatting) and the screen layout and painting are C++ with no IDF headers, so pytest can test them on the Mac through a small shared library (see "Host tests").
- All app buffers static and sized once; no `malloc` in app code outside initialisation.
- Minimal dependency surface: three managed components, otherwise IDF built-ins.

**Non-Goals:**
- Pixel parity with the Pi's DejaVu rendering. u8g2 fonts of similar size are used; a custom converted font can come later.
- Power management, deep sleep, burn-in mitigation.
- Any write to the API; login on the device.
- Host tests for the IDF-bound modules (`screen`, `net`, `fetch`, `buttons`); those are checked on the board.

## Decisions

### Modules

```
main/
  esp32-ssd1315-dashboard.cpp  app_main: init order, event loop, state machine
  status.hpp / status.cpp      daily JSON (cJSON) -> Screen, format_elapsed; no IDF headers
  layout.hpp / layout.cpp      baselines of every screen, computed once from the fonts' u8g2 metrics
  draw.hpp / draw.cpp          paints a Screen into a u8g2 buffer at those baselines; no IDF headers
  screen.hpp / screen.cpp      u8g2 setup on esp_driver_i2c, owns the u8g2_t
  net.hpp / net.cpp            wifi station + reconnect, posts WIFI_UP/WIFI_DOWN
  fetch.hpp / fetch.cpp        esp_http_client GET -> static body buffer -> status
  buttons.hpp / buttons.cpp    espressif/button handles, posts BTN_REFRESH
  Kconfig.projbuild            the menuconfig options
  idf_component.yml            u8g2, espressif/cjson, espressif/button
components/
  etl/                         wrapper component for the Embedded Template Library
host_tests/
  shim.cpp                     extern "C" wrapper over status/layout/draw for ctypes
  CMakeLists.txt               builds libdash_host.dylib into build/host_tests/
  .clangd                      points clangd at the host build's compile_commands.json
  conftest.py                  configures and builds, loads the library, renders frames
  test_draw.py, test_status.py
```

Everything is C++ (the project was meant to be created with `--cpp`): `extern "C"` appears only on `app_main` and on the callbacks that u8g2, iot_button and esp_event call back into. No exceptions, no RTTI, and no heap-owning standard containers; ETL's fixed-capacity types take that role. IDF 6.1 builds C++ as gnu++26, so in-order designated initialisers fill the IDF config structs. IDF's `-Wextra -Werror` makes GCC reject a C++ designated initialiser that omits a field (`-Wmissing-field-initializers`), so every field is listed in declaration order; a field left at its zero default carries a comment saying it does not matter and is listed only so the initialiser is complete, e.g. `.scl_wait_us = 0, // default, listed for completeness`.

`status`, `layout` and `draw` have no IDF includes, so the host tests build them for the Mac (see "Host tests").

### ETL (Embedded Template Library)

The operator is learning ETL and the library matches the no-heap-in-app rule. It is used where it replaces something hand-rolled:

- `etl::string<N>` for the screen texts (fixed capacity, `c_str()` for u8g2), `etl::string_view` for inputs, `etl::to_string`/`etl::format_spec` for numbers.
- `etl::expected<Screen, FetchError>` as the result of parsing and fetching, instead of out-parameters plus an error enum.
- `etl::span<char>` for the HTTP body buffer, `etl::delegate` where a callback is stored.
- `etl::vector<uint8_t, 32>` for the I2C transfer the u8g2 byte callback collects, instead of an array plus a length; an overflow logs `vector_full` through the error handler and drops the chunk instead of writing past the buffer.
- `etl::error_handler` with a callback that logs through `ESP_LOGE`; the profile enables `ETL_LOG_ERRORS`, `ETL_CHECK_PUSH_POP` and `ETL_VERBOSE_ERRORS`, so a bounds violation shows up in the monitor instead of being compiled out (exceptions stay off).
- Not used for the main state machine or the event queue: the FreeRTOS queue is what lets the main task block, and a `switch` on an `enum class` is easier to read than `etl::fsm` for four states. `etl::fsm` can be a later refactor if the operator wants to try it.

Packaging: ETL is header-only but its root `CMakeLists.txt` is a plain CMake project (`project()` + an INTERFACE target), not an IDF component. IDF includes every component's `CMakeLists.txt` in script mode during early expansion, where `project()` fails, and a component that never calls `idf_component_register` is dropped silently (`tools/cmake/build.cmake`, `__build_expand_requirements`). So a git dependency in `idf_component.yml` cannot work. `components/etl/CMakeLists.txt` instead uses CMake `FetchContent` to populate the ETL repository at tag `20.49.0` during the real configure pass (returning early in `CMAKE_BUILD_EARLY_EXPANSION`), and registers `<etl>/include` plus `components/etl/profile` (home of `etl_profile.h`, which ETL finds through the include path) as `INCLUDE_DIRS`. The clone lands in `build/esp-idf/etl/etl-src` (the direct form of `FetchContent_Populate` uses the component's binary directory, not `_deps`); `fullclean` re-fetches it.

- *Alternative*: `marcel-cd/etlcpp` from the component registry. It wraps an unpinned ETL submodule, adds Xtensa compile flags and an `ExternalProject` build step that a header-only library does not need. Rejected.
- *Alternative*: a git submodule under `components/etl/etl`. Browsable in the repo and no network at configure time, but the operator has to run `git submodule add` and every clone needs `--recursive`. FetchContent gives the same pinning with no extra step; switching later is a two-line change.

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

- The main task is the only writer of the display and the only owner of the model (`Screen current`, `bool prompt`, `wifi_state`). No mutex around u8g2.
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

- `u8g2_Setup_ssd1306_i2c_128x64_noname_f` with the byte callback and GPIO/delay callback from the IDF `i2c_u8g2` example (bus created with `i2c_new_master_bus`, device added in `U8X8_MSG_BYTE_INIT`, internal pull-ups on). The setup points the static `u8g2_t` at u8g2's own static 1 KB frame buffer (`u8g2_m_16_8_f`), so the display needs no heap. Its transfer layer, `u8x8_cad_ssd13xx_fast_i2c`, sends at most 25 bytes per I2C transfer (24 data bytes plus the control byte), which the byte callback collects in a 32-byte `etl::vector`. `screen::init()` clears the controller RAM before switching the panel on, so power-up noise never shows, and returns `ESP_FAIL` when adding the I2C device failed (u8g2's calls return nothing, so it checks that the device handle was set); `screen::show(const Screen&, bool prompt)` is the only other entry point.
- Fonts, chosen to keep the Pi's proportions on 64 pixels: `u8g2_font_helvR12_tr` for `since` and `missing N`, `u8g2_font_logisoso26_tr` for the big line (includes `h` and `m`), `u8g2_font_helvR14_tr` for centred messages (`no wakeup`, errors, boot texts), `u8g2_font_5x7_tr` for `refresh?`. These are starting candidates: two `helvR12` boxes (16 px each) plus 2 px gaps leave 28 px for the number, so `logisoso26` is the largest that fits. The task that places the text records the final choice here.
- Vertical layout lives in `layout.hpp/.cpp` and is computed once at start-up, in u8g2's own vocabulary: `u8g2_SetFont` then `u8g2_GetAscent`/`u8g2_GetDescent` (descent is negative) for each font, with four small helpers (`top_pixel`, `bottom_pixel`, `baseline_for_top`, `baseline_for_bottom`) as the only place the descent sign is handled. `StatusLayout::from(u8g2, small, big, gap)` yields three named baselines: `since` flush with the top edge, `missing N` flush with the bottom edge, the big number centred in the band between them with at least `gap` (2) free pixels on each side. The big number reserves only `ascent + 1` pixels, because digits, `h` and `m` sit on the baseline and the font's descent would push it up for nothing. `MessageLayout::from(u8g2, message, small, gap)` yields a single centred baseline (full glyph box, descenders included) and two baselines for the SSID-over-`try N` block in the small font; `prompt_baseline` puts `refresh?` flush with the top edge. The painter in `draw.cpp` only sets the font, centres or right-aligns with `u8g2_GetStrWidth`, and draws at the named baseline. An SSID wider than the panel is cut by `u8g2_DrawStr`'s natural clipping, nothing is wrapped.
- *Alternatives considered*: a generic band/placement layout engine (rejected: the three lines have fixed fonts, so there is no per-call variability to abstract), hardwired baseline constants (rejected: they still have to be derived from the same ascent/descent, so computing them once costs nothing and survives a font change), and a `FontMetrics` struct with renamed, sign-flipped fields (rejected: a second vocabulary for u8g2's ascent/descent).
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

### JSON to screen (`status.cpp`)

Same rules as `sittings.py`:

1. `cJSON_ParseWithLength(body, len)`; failure → `bad response`.
2. `stats` absent or not object/null → `bad response`; `stats` is JSON null → message `no wakeup`.
3. `timeSince` is an array of two numbers → `elapsed = h*60 + m`; null → `elapsed = fromWakeupMins` (must be a number); anything else → `bad response`.
4. `sittingDebt` must be a number; `missing = max(0, floor(debt))`; `elapsed = max(0, elapsed)`.
5. `format_elapsed(minutes)`: `<60` → `%dm`, `<180` → `%dh%02d`, else `%dh`.
6. `cJSON_Delete` before returning. The tree is transient inside one fetch, so cJSON's heap use leaves nothing behind. (A static arena through `cJSON_InitHooks` was considered and dropped: the operator's rule is "libraries may allocate", and the arena would only add a failure mode.)

`Screen` is a small value type: `kind` (status / message), `etl::string<7> elapsed`, `int missing`, `etl::string<32> line1`, `etl::string<15> line2` (the SSID and `try N` for the two-line boot screen). `screen_from_daily` returns `etl::expected<Screen, FetchError>`, where `FetchError` holds the kind (`no_network`, `bad_token`, `server`, `bad_response`), the HTTP status for `server`, and `text()` gives the panel line; the main task copies the value into its model, nothing points into the parsed tree.

### Host tests

`host_tests/` checks the IDF-free modules on the Mac with pytest, from the repo's `.venv` (`pytest` and `pillow` in the root `pyproject.toml`'s dev group).

- `CMakeLists.txt` is a standalone CMake project (IDF scans only `main/` and `components/`, so it never picks it up), configured with Ninja into `build/host_tests/` and built with the system `cc`/`c++`. u8g2's graphics core (`u8g2_*.c`, `u8x8_*.c` from `managed_components/`, so one `idf.py build` must have run first) becomes a static library; `layout.cpp`, `draw.cpp` and `shim.cpp` (and, from task 4.1, `status.cpp` and cJSON) become `libdash_host.dylib`. ETL comes from its own `FetchContent` at the same tag `20.49.0`: its root `CMakeLists.txt` works as a plain CMake project here, outside IDF's script mode. CMake tracks header dependencies, so only what changed is rebuilt.
- `CMAKE_EXPORT_COMPILE_COMMANDS` writes `build/host_tests/compile_commands.json`, and `host_tests/.clangd` points clangd at it, so the shim gets diagnostics from the Mac's clang instead of the IDF cross build's database.
- `shim.cpp` is the only new C++: `draw_screen` and `screen_from_daily` take and return C++ types that ctypes cannot build, so it exposes a flat `extern "C"` API: set up a `u8g2_t` with byte and GPIO callbacks that accept everything, call `draw_init`, draw a status or message screen or one string at a given position, return the layouts and the frame buffer pointer, and (task 4.1) run `format_elapsed` and `screen_from_daily` into a plain C struct.
- `conftest.py` runs `cmake -S … -B … -G Ninja` and `cmake --build` once per session (a no-op build when nothing changed), loads the library with `ctypes` and turns the 1 KB page buffer (8 pages × 128 columns, one byte per 8 vertical pixels, LSB on top) into a 128×64 grid. Each drawn frame is also saved as a scaled-up PNG under `build/host_tests/frames/` for viewing.
- Checks on frames: nothing clipped (the frame's lit pixels equal the sum of each string drawn alone well inside the panel), the big line centred within ±1 px by its ink, and at least 2 empty rows between the big line and the lines above and below.
- The four fonts move from `draw.cpp`'s anonymous namespace into `draw.hpp` (`dash::fonts`), so the painter and the tests cannot drift apart after a font change.
- *Alternative*: a C++ test program in the scratchpad (the original plan). No new files in the repo, but it is thrown away, so nothing re-checks the layout after the font tuning on the real panel, and generating JSON bodies for task 4.1 is clumsier in C++.
- *Alternative*: a plain `Makefile`. One command instead of two, but pattern rules, header dependency tracking and the object directory are written by hand, and it gives clangd nothing.
- *Alternative*: GoogleTest or Catch2. A second test runner and a fetched dependency, when the repo already has Python tooling. `cffi`/`pybind11` instead of `ctypes`: extra dependencies for a dozen functions.

### Buttons

- `espressif/button`: one `iot_button_new_gpio_device` per configured GPIO, active low with the internal pull-up. The refresh button registers `BUTTON_PRESS_DOWN`; the callback posts `BTN_REFRESH` to the queue (the callback runs in the component's own task, so it may block briefly on the queue). The three spare buttons are created with no callbacks, which still configures and debounces the GPIO.
- The confirm window is a one-shot `esp_timer` started on the first press; its callback posts `PROMPT_TIMEOUT`. The component's own double-click event is not used because it reports only after the second press and could not show the prompt.
- Defaults: refresh on GPIO 0, spares on GPIO 1, 2, 3; SDA GPIO 6, SCL GPIO 7, 400 kHz, address 0x3C. All are `menuconfig` options.

### Kconfig

`main/Kconfig.projbuild`, menu "Sitting dashboard", options prefixed `DASH_`: `I2C_SDA`, `I2C_SCL`, `I2C_FREQ_HZ`, `I2C_ADDRESS` (hex), `BTN_REFRESH_GPIO`, `BTN_SPARE1_GPIO`, `BTN_SPARE2_GPIO`, `BTN_SPARE3_GPIO`, `API_SCHEME` (choice http/https), `API_HOST`, `API_PORT`, `API_TOKEN`, `WIFI_SSID`, `WIFI_PASSWORD`, `REFRESH_SECONDS` (range 5–3600), `CONFIRM_MS` (range 200–5000). GPIO options use the `ENV_GPIO_RANGE_*` bounds via the same `orsource` line as the IDF example.

### `sdkconfig.defaults` and git

Committed `sdkconfig.defaults`: `CONFIG_IDF_TARGET="esp32c6"`, `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y`, `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y`, and nothing secret. The pin and host defaults live in the Kconfig `default` lines, not here, so `menuconfig` shows them as defaults rather than as overrides. `.gitignore` gains `esp32-ssd1315-dashboard/sdkconfig` (it will hold the passphrase and the token) and `esp32-ssd1315-dashboard/managed_components/`; `dependencies.lock` is committed so builds are reproducible. The already staged `sdkconfig` has to be removed from the index by the operator; the implementation agent cannot run git.

*Alternative*: keep `sdkconfig` committed and put the secrets in a separate `sdkconfig.secrets` listed in `SDKCONFIG_DEFAULTS`. That splits one `menuconfig` session across two files and is easy to get wrong; a single ignored `sdkconfig` is the common IDF practice.

## Risks / Trade-offs

- [Flash] Wi-Fi + TLS + certificate bundle + u8g2 fonts may approach the 1.5 MB large app partition. → Check `idf.py size` after the first full build; `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN` (common CAs only) and dropping unused u8g2 fonts (only the four named ones are linked) are the levers.
- [TLS date check] If `CONFIG_MBEDTLS_HAVE_TIME_DATE` is ever enabled, TLS fails until the clock is set. → A NOTE in `sdkconfig.defaults` and the README; SNTP is a later change if wanted.
- [Response growth] A day with more than ~200 sittings would overflow the 16 KB buffer and show `bad response`. → Out of range for a real day; the buffer size is one constant.
- [Token lifetime] If the server expires tokens, the panel shows `bad token` until the operator issues a new one and reflashes. → Accepted for now; login on the device is deferred until the endpoint stabilises.
- [Heap over weeks] TLS handshakes every 30 s churn the heap. → Minimum free heap is logged after each fetch; the reused HTTP handle keeps lwIP/TLS allocations bounded. If fragmentation shows up, keep-alive or a longer interval are the knobs.
- [Button task blocking] The button callback posts to the queue from the component's task; if the queue were full it would block that task. → Queue depth 8 with the main task draining promptly; a post uses a zero timeout and drops the press with a log line rather than blocking.
- [Pin defaults] The C6-Zero pin facts come from the vendor wiki, not from the board in hand. → Defaults are `menuconfig` options; the README tells the operator to check them.
- [ETL fetch needs the network] A fresh `build/` configures only with GitHub reachable. → The tag is pinned, so the result is reproducible; an offline build can point `FETCHCONTENT_SOURCE_DIR_ETL` at a local checkout.

## Migration Plan

Additive. Deploy: `idf.py set-target esp32c6` (once), `idf.py menuconfig` to enter SSID, passphrase, token and any pin changes, `idf.py -p <port> flash monitor`. The Pi setup is untouched and keeps working in parallel. Rollback: flash any earlier firmware or erase the flash; nothing outside the board changes.

## Open Questions

- Which u8g2 font sizes make the layout look closest to the Pi on the real SSD1315 is a judgement call to make with the panel in hand; the candidates above are the starting point and the task that places the text records the final choice here; the host tests re-check clipping, centring and gaps after each change.

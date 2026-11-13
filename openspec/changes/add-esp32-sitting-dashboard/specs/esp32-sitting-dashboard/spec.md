# Spec Delta

## Purpose

Standalone firmware for the Waveshare ESP32-C6-Zero that connects to Wi-Fi, fetches the operator's sitting status from the sitting tracker API and shows it on an SSD1315 128x64 panel, with four buttons and every hardware and network setting chosen at build time.

## ADDED Requirements

### Requirement: Build-time configuration
Every hardware and network setting SHALL be a `menuconfig` option of the ESP-IDF project: I2C SDA and SCL GPIOs, panel I2C address, I2C frequency, the GPIO of each of the four buttons, URL scheme (`http` or `https`), host, port, Wi-Fi SSID, Wi-Fi passphrase, API token, refresh interval in seconds, and the refresh-confirm window in milliseconds. Changing any of them SHALL require only a rebuild and flash, no source edit.

#### Scenario: Pins changed through menuconfig
- **WHEN** the operator sets different SDA/SCL GPIOs in `idf.py menuconfig`, rebuilds and flashes
- **THEN** the firmware drives the panel on the new pins without any source change

#### Scenario: Shipped defaults
- **WHEN** the project is built with its committed defaults
- **THEN** the scheme is `https`, the host is `sitting.lviv.social`, the port is `443`, the refresh interval is 30 s and the confirm window is 1000 ms

#### Scenario: Secrets stay out of git
- **WHEN** the operator enters the Wi-Fi passphrase and the API token in `menuconfig`
- **THEN** they are written only to `sdkconfig`, which git ignores, and the committed defaults file holds no credentials

### Requirement: Empty credentials
When the configured Wi-Fi SSID or API token is empty, the firmware SHALL show `no config` on the panel and SHALL NOT attempt to connect or fetch.

#### Scenario: Token not set
- **WHEN** the firmware is built with an empty API token and boots
- **THEN** the panel shows `no config` and stays there

### Requirement: Panel shows the boot progress
The firmware SHALL initialise the panel before anything else and SHALL show each stage of start-up as it happens, so the panel is never blank once the panel driver is up: `booting` during initialisation, the Wi-Fi SSID while connecting, then `fetching` until the first status arrives.

#### Scenario: Normal boot
- **WHEN** the board is powered with valid configuration and the network available
- **THEN** the panel shows `booting`, then the configured SSID, then `fetching`, then the status, in that order

#### Scenario: Wi-Fi retries during boot
- **WHEN** the first connection attempt fails
- **THEN** the panel keeps showing the SSID together with the attempt number, which increases with each retry

### Requirement: Wi-Fi station with automatic reconnect
The firmware SHALL connect to the configured network as a Wi-Fi station and SHALL keep retrying the connection, without limit, whenever it is lost.

#### Scenario: Access point comes back
- **WHEN** the access point is switched off and on again
- **THEN** the firmware reconnects on its own, without a reset

### Requirement: Status fetched from the API with a bearer token
The firmware SHALL fetch the status with `GET <scheme>://<host>:<port>/api/v1/daily`, sending the configured token as `Authorization: Bearer <token>`. It SHALL NOT call the login endpoint. Over `https` it SHALL verify the server certificate against the bundled certificate store.

#### Scenario: Request shape
- **WHEN** the firmware fetches the status
- **THEN** the server receives one `GET /api/v1/daily` with the `Authorization: Bearer <token>` header and no request to `/api/v1/login`

#### Scenario: Plain HTTP for a local server
- **WHEN** the scheme is `http`, the host `192.168.1.10` and the port `3010`
- **THEN** the firmware fetches `http://192.168.1.10:3010/api/v1/daily` without TLS

### Requirement: Status screen as on the Pi
The status screen SHALL follow the `sitting-status` capability's rules for the elapsed time format, the missing count and the time since wakeup before the first sitting, and the same layout: a `since` label at the top left, the elapsed time as the large centred line, and `missing N` at the bottom left. Only the response's `stats` object SHALL be used; the other keys SHALL be ignored.

#### Scenario: Status drawn
- **WHEN** the response's `stats.timeSince` is `[2, 15]` and `stats.sittingDebt` is `2.6`
- **THEN** the panel shows `since`, a large `2h15` and `missing 2`

#### Scenario: Before the first sitting
- **WHEN** `stats.timeSince` is `null` and `stats.fromWakeupMins` is `100`
- **THEN** the large line shows `1h40`

#### Scenario: No wakeup
- **WHEN** `stats` is `null`
- **THEN** the panel shows `no wakeup`

#### Scenario: Large response
- **WHEN** the response holds 100 entries in `sittings`
- **THEN** the status is still drawn from `stats`

### Requirement: Periodic refresh without flicker
The firmware SHALL fetch the status every refresh interval. While a fetch is in progress the previous screen SHALL stay on the panel unchanged; the panel SHALL be redrawn only when the fetch has a result.

#### Scenario: Numbers update in place
- **WHEN** a scheduled fetch returns a new elapsed time
- **THEN** the panel changes directly from the old numbers to the new ones, with no intermediate message

#### Scenario: Interval respected
- **WHEN** the refresh interval is 30 s and the network is healthy
- **THEN** the server receives one request about every 30 s

### Requirement: Errors replace the status
When a fetch fails, the firmware SHALL replace the panel content with a short error text and log the details to the serial console: `no network` when the server cannot be reached or does not answer within the request timeout, `bad token` on HTTP 401 or 403, `server <code>` on any other non-2xx status, `bad response` when the body is not JSON, lacks the expected `stats` fields or is too large. The log SHALL never contain the token.

#### Scenario: Server down
- **WHEN** a scheduled fetch cannot connect to the server
- **THEN** the panel shows `no network` instead of the previous numbers

#### Scenario: Token rejected
- **WHEN** the server answers 401
- **THEN** the panel shows `bad token`

#### Scenario: Gateway error
- **WHEN** the server answers 502
- **THEN** the panel shows `server 502`

#### Scenario: Malformed body
- **WHEN** the server answers 200 with a body that is not JSON
- **THEN** the panel shows `bad response`

#### Scenario: Recovery
- **WHEN** the next scheduled fetch after an error succeeds
- **THEN** the status replaces the error text

### Requirement: Bounded fetch time
A single fetch SHALL give up within 15 s when the server does not respond, and fetches SHALL never overlap.

#### Scenario: Server accepts and hangs
- **WHEN** the server accepts the connection but never answers
- **THEN** the panel shows `no network` within 15 s and the next scheduled fetch starts on time

### Requirement: Wi-Fi loss shown immediately
When the Wi-Fi connection is lost, the firmware SHALL show `no wifi` at once, without waiting for the next fetch. When the connection is back, it SHALL fetch the status immediately instead of waiting for the next interval.

#### Scenario: Link drops
- **WHEN** the access point disappears while the status is on the panel
- **THEN** the panel shows `no wifi` within a few seconds

#### Scenario: Link returns
- **WHEN** the connection is re-established
- **THEN** the status is fetched right away and replaces `no wifi` as soon as it arrives

### Requirement: Refresh button with confirmation
A first press of the refresh button SHALL show `refresh?` in the top-right corner of the panel in the smallest font, on top of whatever is shown, without disturbing it. A second press within the confirm window SHALL remove the prompt and start a fetch at once. Without a second press, the prompt SHALL disappear when the window expires and nothing is fetched.

#### Scenario: Confirmed refresh
- **WHEN** the operator presses the refresh button twice within the confirm window
- **THEN** `refresh?` appears after the first press, disappears after the second, and a fetch starts immediately, with the current numbers staying on the panel until its result arrives

#### Scenario: Press not confirmed
- **WHEN** the operator presses the refresh button once and waits longer than the confirm window
- **THEN** `refresh?` disappears and no request is sent

#### Scenario: Prompt over an error
- **WHEN** `no network` is on the panel and the operator presses the refresh button once
- **THEN** `refresh?` appears in the top-right corner while `no network` stays visible

#### Scenario: Debounced
- **WHEN** the operator presses the button once with normal contact bounce
- **THEN** it counts as one press, not as a confirmed refresh

### Requirement: Spare buttons reserved
The three other buttons SHALL be configured as debounced inputs on their configured GPIOs and SHALL have no effect on the panel in this version.

#### Scenario: Spare button pressed
- **WHEN** the operator presses one of the three spare buttons
- **THEN** the panel content does not change

### Requirement: Serial log of state changes
The firmware SHALL log each boot stage, Wi-Fi connect and disconnect, fetch result and error detail to the serial console, together with the minimum free heap after each fetch, so an operator attached with `idf.py monitor` can follow the device's state.

#### Scenario: Monitoring a refresh
- **WHEN** a scheduled fetch completes while `idf.py monitor` is attached
- **THEN** the console shows a line with the outcome and the minimum free heap

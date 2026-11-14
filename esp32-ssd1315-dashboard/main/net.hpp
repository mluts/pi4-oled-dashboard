// Wi-Fi station on the network from menuconfig; reconnects without limit.
#pragma once

#include "esp_err.h"
#include "etl/delegate.h"

namespace dash::net {

// Runs in the default event loop's task: up (attempt 0) once the station has an
// address, down with the failed attempts so far after every disconnect.
using LinkCallback = etl::delegate<void(bool up, int attempt)>;

// NVS, netif, default event loop and the station; connecting starts at once.
esp_err_t start(LinkCallback on_link);

} // namespace dash::net

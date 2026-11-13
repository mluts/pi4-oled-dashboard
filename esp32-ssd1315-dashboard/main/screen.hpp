// The SSD1315 panel on the I2C pins from menuconfig. Only the main task calls
// show().
#pragma once

#include "esp_err.h"

#include "status.hpp"

namespace dash::screen {

// Creates the I2C bus, initialises the panel and switches it on.
esp_err_t init();

// Redraws the whole panel from the screen (plus the "refresh?" overlay).
void show(const Screen &screen, bool prompt);

}  // namespace dash::screen

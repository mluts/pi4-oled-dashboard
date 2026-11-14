// The four buttons from menuconfig: active low, internal pull-ups, debounced
// by espressif/button. Only the refresh button does anything yet.
#pragma once

#include "esp_err.h"
#include "etl/delegate.h"

namespace dash::buttons {

// Runs in the button component's timer context on each press of the refresh
// button (BUTTON_PRESS_DOWN, after debounce).
using PressCallback = etl::delegate<void()>;

// Creates all four buttons; the three spares get no callback.
esp_err_t start(PressCallback on_refresh);

} // namespace dash::buttons

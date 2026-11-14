#include "buttons.hpp"

#include <initializer_list>

#include "button_gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "iot_button.h"
#include "sdkconfig.h"

namespace dash::buttons {
namespace {
const char *TAG = "buttons";

PressCallback s_on_refresh;

void on_press(void *, void *) { s_on_refresh(); }

// One button to ground on gpio, with the internal pull-up. The component polls
// and debounces it, so contact bounce on one press yields one PRESS_DOWN.
esp_err_t make(int gpio, button_handle_t &out) {
  const button_config_t config = {
      .long_press_time = 0,  // default, listed for completeness
      .short_press_time = 0, // default, listed for completeness
  };
  const button_gpio_config_t gpio_config = {
      .gpio_num = gpio,
      .active_level = 0,          // pressed pulls the pin to ground
      .enable_power_save = false, // default, listed for completeness
      .disable_pull = false,      // keep the internal pull-up
  };
  return iot_button_new_gpio_device(&config, &gpio_config, &out);
}

} // namespace

esp_err_t start(PressCallback on_refresh) {
  s_on_refresh = on_refresh;

  button_handle_t refresh;
  ESP_RETURN_ON_ERROR(make(CONFIG_DASH_BTN_REFRESH_GPIO, refresh), TAG,
                      "creating the refresh button on GPIO %d failed",
                      CONFIG_DASH_BTN_REFRESH_GPIO);
  ESP_RETURN_ON_ERROR(iot_button_register_cb(refresh, BUTTON_PRESS_DOWN,
                                             nullptr, on_press, nullptr),
                      TAG, "registering the refresh callback failed");

  // The spares get no callback, which still configures and debounces the pin.
  for (int gpio : {CONFIG_DASH_BTN_SPARE1_GPIO, CONFIG_DASH_BTN_SPARE2_GPIO,
                   CONFIG_DASH_BTN_SPARE3_GPIO}) {
    button_handle_t spare;
    ESP_RETURN_ON_ERROR(make(gpio, spare), TAG,
                        "creating the spare button on GPIO %d failed", gpio);
  }

  ESP_LOGI(TAG, "refresh on GPIO %d, spares on GPIO %d, %d, %d",
           CONFIG_DASH_BTN_REFRESH_GPIO, CONFIG_DASH_BTN_SPARE1_GPIO,
           CONFIG_DASH_BTN_SPARE2_GPIO, CONFIG_DASH_BTN_SPARE3_GPIO);
  return ESP_OK;
}

} // namespace dash::buttons

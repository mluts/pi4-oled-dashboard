#include "screen.hpp"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "hal/i2c_types.h"
#include "sdkconfig.h"
#include "u8g2.h"

#include "draw.hpp"
#include "u8x8.h"

namespace dash::screen {
namespace {
const char *TAG = "screen";

constexpr int kI2CTimeoutMs = 1000;

i2c_master_bus_handle_t s_bus;
i2c_master_dev_handle_t s_dev;

// NOTE: the frame buffer lives inside u8g2_t for the _f setup, so this is the
// only display memory in the app
u8g2_t s_u8g2;

uint8_t byte_cb(u8x8_t *, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
  // NOTE: u8x8_cad_ssd13xx_i2c sends at most 24 data bytes plus one control
  // byte per transfer (u8x8_cad.c), the same for SSD1306 and SSD1315
  // CAD - command, arg, data
  static uint8_t buffer[32];
  static uint8_t length;

  switch (msg) {
  case U8X8_MSG_BYTE_INIT: {
    i2c_device_config_t dev_config = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                      .device_address = CONFIG_DASH_I2C_ADDRESS,
                                      .scl_speed_hz = CONFIG_DASH_I2C_FREQ_HZ};
    if (i2c_master_bus_add_device(s_bus, &dev_config, &s_dev) != ESP_OK) {
      ESP_LOGE(TAG, "adding the panel at 0x%023x failed",
               CONFIG_DASH_I2C_ADDRESS);
    }
    break;
  }

  case U8X8_MSG_BYTE_START_TRANSFER:
    length = 0;
    break;

  case U8X8_MSG_BYTE_SET_DC:
    break; // the SSD1306 takes D/C as the control byte u8g2 already sends

  case U8X8_MSG_BYTE_SEND: {
    auto *data = static_cast<const uint8_t *>(arg_ptr);

    for (uint8_t i = 0; i < arg_int && length < sizeof(buffer); i++) {
      buffer[length++] = data[i];
    }
    break;
  }

  case U8X8_MSG_BYTE_END_TRANSFER:
    if (length > 0 && s_dev != nullptr) {
      esp_err_t err = i2c_master_transmit(s_dev, buffer, length, kI2CTimeoutMs);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c transmit failed: %s", esp_err_to_name(err));
        return 0;
      }
    }
    break;

  default:
    return 0;
  }

  return 1;
}

uint8_t gpio_delay_cb(u8x8_t *, uint8_t msg, uint8_t arg_int, void *) {
  switch (msg) {
  case U8X8_MSG_GPIO_AND_DELAY_INIT:
  case U8X8_MSG_GPIO_RESET:
    break; // no reset line on the module
  case U8X8_MSG_DELAY_MILLI:
    vTaskDelay(pdMS_TO_TICKS(arg_int));
  }
}
} // namespace

} // namespace dash::screen

#include "screen.hpp"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "etl/vector.h"
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

// One I2C transfer, collected by byte_cb between START and END_TRANSFER.
// NOTE: the CAD (command/arg/data) layer of the ssd1306 setup,
// u8x8_cad_ssd13xx_fast_i2c, sends at most 24 data bytes plus one control
// byte per transfer, or a control byte, a command and its few args
// (u8x8_cad.c).
etl::vector<uint8_t, 32> s_transfer;

// NOTE: the _f setup points this at u8g2's own static 1 KB frame buffer
// (u8g2_m_16_8_f in u8g2_d_memory.c), so the display needs no heap.
u8g2_t s_u8g2;

uint8_t byte_cb(u8x8_t *, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
  switch (msg) {
  case U8X8_MSG_BYTE_INIT: {
    i2c_device_config_t dev_config = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                      .device_address = CONFIG_DASH_I2C_ADDRESS,
                                      .scl_speed_hz = CONFIG_DASH_I2C_FREQ_HZ};
    esp_err_t err = i2c_master_bus_add_device(s_bus, &dev_config, &s_dev);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "adding the panel at 0x%02x failed: %s",
               CONFIG_DASH_I2C_ADDRESS, esp_err_to_name(err));
      return 0;
    }
    break;
  }

  case U8X8_MSG_BYTE_START_TRANSFER:
    s_transfer.clear();
    break;

  case U8X8_MSG_BYTE_SET_DC:
    break; // the SSD1306 takes D/C as the control byte u8g2 already sends

  case U8X8_MSG_BYTE_SEND: {
    auto *data = static_cast<const uint8_t *>(arg_ptr);
    s_transfer.insert(s_transfer.end(), data, data + arg_int);
    break;
  }

  case U8X8_MSG_BYTE_END_TRANSFER:
    if (!s_transfer.empty() && s_dev != nullptr) {
      esp_err_t err = i2c_master_transmit(s_dev, s_transfer.data(),
                                          s_transfer.size(), kI2CTimeoutMs);
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
    break;
  default:
    return 0; // the delays below 1 ms are only used by bit-banged buses
  }

  return 1;
}
} // namespace

esp_err_t init() {
  ESP_LOGI(TAG, "panel at 0x%02x on SDA %d / SCL %d, %d Hz",
           CONFIG_DASH_I2C_ADDRESS, CONFIG_DASH_I2C_SDA, CONFIG_DASH_I2C_SCL,
           CONFIG_DASH_I2C_FREQ_HZ);

  i2c_master_bus_config_t bus_config = {
      .i2c_port = I2C_NUM_0,
      .sda_io_num = static_cast<gpio_num_t>(CONFIG_DASH_I2C_SDA),
      .scl_io_num = static_cast<gpio_num_t>(CONFIG_DASH_I2C_SCL),
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags = {.enable_internal_pullup = true},
  };
  ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_bus), TAG,
                      "creating the i2c bus failed");

  u8g2_Setup_ssd1306_i2c_128x64_noname_f(&s_u8g2, U8G2_R0, byte_cb,
                                         gpio_delay_cb);
  u8g2_InitDisplay(&s_u8g2);
  if (s_dev == nullptr) {
    return ESP_FAIL; // byte_cb has logged why
  }
  // NOTE: the controller's RAM holds noise after power-up; clear it while the
  // panel is still off so switching on never flashes garbage.
  u8g2_ClearDisplay(&s_u8g2);
  u8g2_SetPowerSave(&s_u8g2, 0);
  draw_init(s_u8g2);

  return ESP_OK;
}

void show(const Screen &screen, bool prompt) {
  draw_screen(s_u8g2, screen, prompt);
}

} // namespace dash::screen

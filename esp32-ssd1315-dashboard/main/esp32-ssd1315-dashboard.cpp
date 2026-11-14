#include "esp_log.h"
#include "etl/error_handler.h"
#include "sdkconfig.h"

#include "screen.hpp"
#include "status.hpp"

static const char *TAG = "dash";

namespace {

// Failed ETL_ASSERTs end up here (ETL_LOG_ERRORS in etl_profile.h).
void on_etl_error(const etl::exception &e)
{
    ESP_LOGE(TAG, "etl: %s (%s:%d)", e.what(), e.file_name(), e.line_number());
}

}  // namespace

extern "C" void app_main(void) {
  etl::error_handler::set_callback<on_etl_error>();

  // The panel comes first so every later stage can show itself.
  if (dash::screen::init() != ESP_OK) {
    return; // nothing to show anything on; screen.cpp has logged why
  }
  ESP_LOGI(TAG, "booting");
  dash::screen::show(dash::Screen::message("booting"), false);

  // NOTE: returning ends only the main task; nothing else runs yet and the
  // panel keeps its last image, so "no config" stays and Wi-Fi never starts.
  if (CONFIG_DASH_WIFI_SSID[0] == '\0' || CONFIG_DASH_API_TOKEN[0] == '\0') {
    ESP_LOGE(TAG,
             "no config: set the Wi-Fi SSID and the API token in menuconfig");
    dash::screen::show(dash::Screen::message("no config"), false);
    return;
  }
}

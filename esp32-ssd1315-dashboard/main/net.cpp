#include "net.hpp"

#include <cstring>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

namespace dash::net {
namespace {
const char *TAG = "net";

LinkCallback s_on_link;
int s_attempts; // failed attempts since the last address

// NOTE: runs in the default event loop's task, not an interrupt, so logging and
// the callback are safe here.
void on_event(void *, esp_event_base_t base, int32_t id, void *data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    ESP_LOGI(TAG, "connecting to %s", CONFIG_DASH_WIFI_SSID);
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    // Both a failed attempt and a dropped link; the reason tells them apart
    // (15 bad passphrase, 201 access point not found, ...).
    auto *e = static_cast<wifi_event_sta_disconnected_t *>(data);
    s_attempts++;
    ESP_LOGW(TAG, "disconnected, reason %d, attempt %d", e->reason, s_attempts);
    s_on_link(false, s_attempts);
    esp_wifi_connect(); // no limit, no backoff: the driver paces it
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    auto *e = static_cast<ip_event_got_ip_t *>(data);
    ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&e->ip_info.ip));
    s_attempts = 0;
    s_on_link(true, 0);
  }
}
} // namespace

esp_err_t start(LinkCallback on_link) {
  s_on_link = on_link;

  // The PHY keeps its RF calibration in NVS. A full or newer-format partition
  // is erased and re-initialised, as in the IDF examples.
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erasing nvs failed");
    err = nvs_flash_init();
  }
  ESP_RETURN_ON_ERROR(err, TAG, "nvs init failed");

  ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
  ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG,
                      "creating the event loop failed");
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init failed");

  // NOTE: registered before esp_wifi_start, or the first STA_START is missed
  // and nothing ever connects.
  ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                          WIFI_EVENT, WIFI_EVENT_STA_START, on_event, nullptr,
                          nullptr),
                      TAG, "registering STA_START failed");
  ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                          WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_event,
                          nullptr, nullptr),
                      TAG, "registering STA_DISCONNECTED failed");
  ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                          IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, nullptr,
                          nullptr),
                      TAG, "registering GOT_IP failed");

  // Zero-filled, so sta.threshold.authmode keeps the IDF default and open,
  // WPA2 and WPA3 networks all work.
  wifi_config_t config = {};
  // NOTE: the fields are raw byte arrays that need no terminating NUL; strncpy
  // fills a 32-byte SSID exactly where strlcpy would cut it to 31.
  std::strncpy(reinterpret_cast<char *>(config.sta.ssid), CONFIG_DASH_WIFI_SSID,
               sizeof(config.sta.ssid));
  std::strncpy(reinterpret_cast<char *>(config.sta.password),
               CONFIG_DASH_WIFI_PASSWORD, sizeof(config.sta.password));
  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG,
                      "setting station mode failed");
  ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG,
                      "setting the wifi config failed");
  ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");

  return ESP_OK;
}

} // namespace dash::net

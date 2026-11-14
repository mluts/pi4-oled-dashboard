#include "fetch.hpp"

#include <cstdio>
#include <cstring>

#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "etl/span.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace dash::fetch {
namespace {
const char *TAG = "fetch";

// NOTE: 100 sittings are about 8 KB; a day would need ~200 to overflow this.
constexpr size_t kBodyCapacity = 16 * 1024;
char s_body_storage[kBodyCapacity];
etl::span<char> s_body{s_body_storage};
size_t s_body_len;
bool s_overflow; // the body did not fit: bad response

DoneCallback s_on_done;
esp_http_client_handle_t s_client;

char s_url[128];  // "<scheme>://<host>:<port>/api/v1/daily"
char s_auth[512]; // "Bearer <token>"; never logged

// NOTE: ESP-IDF counts stack in bytes; TLS handshakes run in this task.
constexpr uint32_t kStackBytes = 8192;
StaticTask_t s_task_tcb;
StackType_t s_task_stack[kStackBytes];
TaskHandle_t s_task;

// Collects the body as it arrives; one perform at a time, in the fetch task.
// NOTE: an overflow keeps returning ESP_OK so the client drains the connection;
// an error here would abort the transfer and read as "no network".
esp_err_t on_http_event(esp_http_client_event_t *e) {
  if (e->event_id == HTTP_EVENT_ON_DATA) {
    size_t len = static_cast<size_t>(e->data_len);
    if (s_overflow || len > s_body.size() - s_body_len) {
      s_overflow = true;
    } else {
      std::memcpy(s_body.data() + s_body_len, e->data, len);
      s_body_len += len;
    }
  }
  return ESP_OK;
}

Result failed(FetchError::Kind kind, int http_status = 0) {
  return etl::unexpected<FetchError>{FetchError{kind, http_status}};
}

// One request and its outcome, mapped as in design.md "HTTP fetch". The detail
// logged is the esp_err_t name or the status code, never a header or the body.
Result perform() {
  s_body_len = 0;
  s_overflow = false;
  esp_err_t err = esp_http_client_perform(s_client);
  int status = esp_http_client_get_status_code(s_client);
  // NOTE: a fresh connection per fetch, so a dropped keep-alive or a timed-out
  // request cannot leave the next one in a bad state.
  esp_http_client_close(s_client);

  if (err != ESP_OK) {
    ESP_LOGW(TAG, "request failed: %s", esp_err_to_name(err));
    return failed(FetchError::Kind::no_network);
  }
  if (status == 401 || status == 403) {
    ESP_LOGW(TAG, "token rejected: HTTP %d", status);
    return failed(FetchError::Kind::bad_token);
  }
  if (status < 200 || status > 299) {
    ESP_LOGW(TAG, "server error: HTTP %d", status);
    return failed(FetchError::Kind::server, status);
  }
  if (s_overflow) {
    ESP_LOGW(TAG, "body over %u bytes", static_cast<unsigned>(kBodyCapacity));
    return failed(FetchError::Kind::bad_response);
  }
  Result result =
      screen_from_daily(etl::string_view(s_body.data(), s_body_len));
  if (!result.has_value()) {
    ESP_LOGW(TAG, "unexpected body, %u bytes",
             static_cast<unsigned>(s_body_len));
  }
  return result;
}

void task_main(void *) {
  for (;;) {
    // pdTRUE clears the count: requests made during a fetch become one more.
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    Result result = perform();
    ESP_LOGI(TAG, "%s, min free heap %u",
             result.has_value() ? "ok" : result.error().text().c_str(),
             static_cast<unsigned>(esp_get_minimum_free_heap_size()));
    s_on_done(result);
  }
}

} // namespace

esp_err_t start(DoneCallback on_done) {
  s_on_done = on_done;

#ifdef CONFIG_DASH_API_SCHEME_HTTPS
  const char *scheme = "https";
#else
  const char *scheme = "http";
#endif
  int n = std::snprintf(s_url, sizeof s_url, "%s://%s:%d/api/v1/daily",
                        scheme, CONFIG_DASH_API_HOST, CONFIG_DASH_API_PORT);
  ESP_RETURN_ON_FALSE(n < static_cast<int>(sizeof s_url), ESP_ERR_INVALID_SIZE,
                      TAG, "the host is too long for the url buffer");
  n = std::snprintf(s_auth, sizeof s_auth, "Bearer %s", CONFIG_DASH_API_TOKEN);
  ESP_RETURN_ON_FALSE(n < static_cast<int>(sizeof s_auth),
                      ESP_ERR_INVALID_SIZE, TAG,
                      "the token is too long for the header buffer");

  // Zero-filled and only the fields that matter set: the struct has about 40,
  // and a designated initialiser would have to list them all.
  esp_http_client_config_t config = {};
  config.url = s_url;
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = 10000; // connect, TLS and each read
  config.event_handler = on_http_event;
  config.crt_bundle_attach = esp_crt_bundle_attach; // ignored for http
  s_client = esp_http_client_init(&config);
  ESP_RETURN_ON_FALSE(s_client != nullptr, ESP_FAIL, TAG,
                      "creating the http client failed");
  ESP_RETURN_ON_ERROR(
      esp_http_client_set_header(s_client, "Authorization", s_auth), TAG,
      "setting the auth header failed");

  ESP_LOGI(TAG, "fetching %s every %d s", s_url, CONFIG_DASH_REFRESH_SECONDS);
  s_task = xTaskCreateStatic(task_main, "fetch", kStackBytes, nullptr, 5,
                             s_task_stack, &s_task_tcb);
  return ESP_OK;
}

void now() { xTaskNotifyGive(s_task); }

} // namespace dash::fetch

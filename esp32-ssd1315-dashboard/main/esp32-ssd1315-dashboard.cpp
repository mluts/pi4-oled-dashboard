#include "esp_log.h"
#include "etl/error_handler.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sdkconfig.h"

#include "net.hpp"
#include "screen.hpp"
#include "status.hpp"

static const char *TAG = "dash";

namespace {

// Failed ETL_ASSERTs end up here (ETL_LOG_ERRORS in etl_profile.h).
void on_etl_error(const etl::exception &e)
{
    ESP_LOGE(TAG, "etl: %s (%s:%d)", e.what(), e.file_name(), e.line_number());
}

// What the main task reacts to. Producers join as their tasks add them.
enum class EventKind { wifi_up, wifi_down };

// NOTE: copied byte by byte through the FreeRTOS queue, so it must stay
// trivially copyable (no etl::string here; see FETCH_DONE in task 4.2).
struct Event {
  EventKind kind;
  int attempt; // wifi_down: failed attempts so far
};

constexpr int kQueueDepth = 8;
StaticQueue_t s_queue_tcb;
uint8_t s_queue_storage[kQueueDepth * sizeof(Event)];
QueueHandle_t s_queue;

// net's LinkCallback, in the event loop task. Waits rather than drops: a lost
// wifi_up would leave the SSID on the panel for good, and the main task does
// nothing but drain the queue.
void on_link(bool up, int attempt) {
  Event e{up ? EventKind::wifi_up : EventKind::wifi_down, attempt};
  xQueueSend(s_queue, &e, portMAX_DELAY);
}

enum class State { connecting, online, offline };

// The SSID alone on the first attempt, then over "try N" for the attempt now
// in progress.
dash::Screen connecting(int attempt) {
  if (attempt == 0) {
    return dash::Screen::message(CONFIG_DASH_WIFI_SSID);
  }
  etl::string<15> line2{"try "};
  etl::to_string(attempt + 1, line2, true);
  return dash::Screen::message(CONFIG_DASH_WIFI_SSID, line2);
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

  s_queue = xQueueCreateStatic(kQueueDepth, sizeof(Event), s_queue_storage,
                               &s_queue_tcb);
  dash::screen::show(connecting(0), false);
  if (dash::net::start(dash::net::LinkCallback::create<on_link>()) != ESP_OK) {
    return; // net.cpp has logged which step failed
  }

  // From here the main task is the event loop and the only one that draws.
  // Every event redraws from the model, never patches the panel.
  State state = State::connecting;
  dash::Screen current = connecting(0);
  for (;;) {
    Event e;
    xQueueReceive(s_queue, &e, portMAX_DELAY);
    switch (e.kind) {
    case EventKind::wifi_up:
      if (state == State::connecting) {
        current = dash::Screen::message("fetching");
      } // offline: "no wifi" stays until FETCH_DONE (task 4.2)
      state = State::online;
      break;
    case EventKind::wifi_down:
      if (state == State::connecting) {
        current = connecting(e.attempt);
      } else {
        state = State::offline;
        current = dash::Screen::message("no wifi");
      }
      break;
    }
    dash::screen::show(current, false);
  }
}

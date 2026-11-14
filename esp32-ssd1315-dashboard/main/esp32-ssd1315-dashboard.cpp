#include <cstdio>
#include <type_traits>

#include "esp_log.h"
#include "esp_timer.h"
#include "etl/error_handler.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sdkconfig.h"

#include "buttons.hpp"
#include "fetch.hpp"
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
enum class EventKind {
  wifi_up,
  wifi_down,
  tick,
  fetch_done,
  btn_refresh,
  prompt_timeout
};

// A fetch result as plain data; the main task rebuilds the Screen from it.
// NOTE: an etl::string keeps a pointer to its own buffer, so a Screen cannot
// travel through the queue's byte copy.
struct FetchOutcome {
  bool ok;
  dash::ScreenKind kind;
  char elapsed[8];
  int missing;
  char line1[33]; // "no wakeup"
  dash::FetchError error;
};

struct Event {
  EventKind kind;
  int attempt;        // wifi_down: failed attempts so far
  FetchOutcome fetch; // fetch_done
};
// Copied byte by byte through the FreeRTOS queue.
static_assert(std::is_trivially_copyable_v<Event>);

constexpr int kQueueDepth = 8;
StaticQueue_t s_queue_tcb;
uint8_t s_queue_storage[kQueueDepth * sizeof(Event)];
QueueHandle_t s_queue;

// net's LinkCallback, in the event loop task. Waits rather than drops: a lost
// wifi_up would leave the SSID on the panel for good, and the main task does
// nothing but drain the queue.
void on_link(bool up, int attempt) {
  Event e{};
  e.kind = up ? EventKind::wifi_up : EventKind::wifi_down;
  e.attempt = attempt;
  xQueueSend(s_queue, &e, portMAX_DELAY);
}

// fetch's DoneCallback, in the fetch task. Waits for the same reason: a lost
// result would leave a stale screen up.
void on_fetch_done(const dash::fetch::Result &result) {
  Event e{};
  e.kind = EventKind::fetch_done;
  FetchOutcome &o = e.fetch;
  o.ok = result.has_value();
  if (o.ok) {
    const dash::Screen &s = result.value();
    o.kind = s.kind;
    std::snprintf(o.elapsed, sizeof o.elapsed, "%s", s.elapsed.c_str());
    o.missing = s.missing;
    std::snprintf(o.line1, sizeof o.line1, "%s", s.line1.c_str());
  } else {
    o.error = result.error();
  }
  xQueueSend(s_queue, &e, portMAX_DELAY);
}

// The refresh timer, in the esp_timer task. A tick dropped on a full queue is
// harmless: the next one follows an interval later.
void on_tick(void *) {
  Event e{};
  e.kind = EventKind::tick;
  xQueueSend(s_queue, &e, 0);
}

// buttons' PressCallback, in the button component's timer, which polls and
// debounces every button: it must not block, so a press is dropped instead.
void on_refresh_press() {
  Event e{};
  e.kind = EventKind::btn_refresh;
  if (xQueueSend(s_queue, &e, 0) != pdTRUE) {
    ESP_LOGW(TAG, "refresh press dropped: queue full");
  }
}

// The confirm window, in the esp_timer task. Waits: a lost timeout would leave
// "refresh?" on the panel.
void on_prompt_timeout(void *) {
  Event e{};
  e.kind = EventKind::prompt_timeout;
  xQueueSend(s_queue, &e, portMAX_DELAY);
}

dash::Screen screen_from(const FetchOutcome &o) {
  if (!o.ok) {
    return dash::Screen::message(o.error.text());
  }
  if (o.kind == dash::ScreenKind::status) {
    return dash::Screen::status(o.elapsed, o.missing);
  }
  return dash::Screen::message(o.line1);
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
  // NOTE: fetch before net, so the fetch task exists when the first wifi_up
  // asks for a fetch.
  if (dash::fetch::start(dash::fetch::DoneCallback::create<on_fetch_done>()) !=
      ESP_OK) {
    return; // fetch.cpp has logged which step failed
  }
  if (dash::net::start(dash::net::LinkCallback::create<on_link>()) != ESP_OK) {
    return; // net.cpp has logged which step failed
  }

  const esp_timer_create_args_t tick_args = {
      .callback = on_tick,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "tick",
      .skip_unhandled_events = true, // no burst of ticks after a stall
  };
  esp_timer_handle_t tick;
  ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick));
  ESP_ERROR_CHECK(esp_timer_start_periodic(
      tick, static_cast<uint64_t>(CONFIG_DASH_REFRESH_SECONDS) * 1000000));

  const esp_timer_create_args_t confirm_args = {
      .callback = on_prompt_timeout,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "confirm",
      .skip_unhandled_events = false, // default, listed for completeness
  };
  esp_timer_handle_t confirm;
  ESP_ERROR_CHECK(esp_timer_create(&confirm_args, &confirm));
  constexpr uint64_t kConfirmUs =
      static_cast<uint64_t>(CONFIG_DASH_CONFIRM_MS) * 1000;

  // NOTE: after the timers, so a press during boot finds them in place.
  if (dash::buttons::start(
          dash::buttons::PressCallback::create<on_refresh_press>()) != ESP_OK) {
    return; // buttons.cpp has logged which step failed
  }

  // From here the main task is the event loop and the only one that draws.
  // Every event redraws from the model (current + prompt), never patches the
  // panel.
  State state = State::connecting;
  dash::Screen current = connecting(0);
  bool prompt = false;
  for (;;) {
    Event e;
    xQueueReceive(s_queue, &e, portMAX_DELAY);
    switch (e.kind) {
    case EventKind::wifi_up:
      if (state == State::connecting) {
        current = dash::Screen::message("fetching");
      } // offline: "no wifi" stays until the result replaces it
      state = State::online;
      dash::fetch::now();
      break;
    case EventKind::tick:
      if (state == State::online) {
        dash::fetch::now();
      }
      continue; // the numbers stay until the result
    case EventKind::fetch_done:
      if (state != State::online) {
        continue; // the link dropped meanwhile: "no wifi" stays
      }
      current = screen_from(e.fetch);
      break;
    case EventKind::wifi_down:
      if (state == State::connecting) {
        current = connecting(e.attempt);
      } else {
        state = State::offline;
        current = dash::Screen::message("no wifi");
      }
      break;
    case EventKind::btn_refresh:
      if (!prompt) { // first press: arm the window
        prompt = true;
        esp_timer_start_once(confirm, kConfirmUs);
        break;
      }
      // Second press within the window: confirm. The numbers stay until the
      // result; offline or connecting, the reconnect fetches anyway.
      prompt = false;
      esp_timer_stop(confirm);
      if (state == State::online) {
        ESP_LOGI(TAG, "refresh confirmed");
        dash::fetch::now();
      }
      break;
    case EventKind::prompt_timeout:
      // NOTE: a timeout queued just before a confirm arrives with the prompt
      // already off and changes nothing; only a third press within those
      // milliseconds would lose its prompt early.
      if (!prompt) {
        continue;
      }
      prompt = false;
      break;
    }
    dash::screen::show(current, prompt);
  }
}

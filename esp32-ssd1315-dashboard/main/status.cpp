// No IDF headers: compiled into the host tests as well as the firmware.
#include "status.hpp"

#include <algorithm>
#include <cmath>

#include "cJSON.h"

namespace dash {
namespace {

using Result = etl::expected<Screen, FetchError>;

Result bad_response() {
  return etl::unexpected<FetchError>{
      FetchError{FetchError::Kind::bad_response}};
}

// Minutes since the last sitting, or since wakeup before the first one; false
// when the fields are missing or mistyped.
bool elapsed_minutes(const cJSON *stats, double &out) {
  const cJSON *since = cJSON_GetObjectItemCaseSensitive(stats, "timeSince");
  if (cJSON_IsArray(since) && cJSON_GetArraySize(since) == 2) {
    const cJSON *h = cJSON_GetArrayItem(since, 0);
    const cJSON *m = cJSON_GetArrayItem(since, 1);
    if (!cJSON_IsNumber(h) || !cJSON_IsNumber(m)) {
      return false;
    }
    out = h->valuedouble * 60 + m->valuedouble;
    return true;
  }
  const cJSON *wakeup =
      cJSON_GetObjectItemCaseSensitive(stats, "fromWakeupMins");
  if (cJSON_IsNull(since) && cJSON_IsNumber(wakeup)) {
    out = wakeup->valuedouble;
    return true;
  }
  return false;
}

// The rules of sittings.py on the parsed tree. Returns before anything is
// freed; the Screen copies what it needs.
// NOTE: cJSON's getters return nullptr for a missing key or a non-object
// parent, and its cJSON_Is* checks are false for nullptr, so absent fields
// fall through to bad_response without extra checks.
Result screen_from_root(const cJSON *root) {
  const cJSON *stats = cJSON_GetObjectItemCaseSensitive(root, "stats");
  if (cJSON_IsNull(stats)) {
    return Screen::message("no wakeup");
  }
  if (!cJSON_IsObject(stats)) {
    return bad_response();
  }

  double elapsed;
  if (!elapsed_minutes(stats, elapsed)) {
    return bad_response();
  }
  const cJSON *debt = cJSON_GetObjectItemCaseSensitive(stats, "sittingDebt");
  if (!cJSON_IsNumber(debt)) {
    return bad_response();
  }

  int missing = std::max(0, static_cast<int>(std::floor(debt->valuedouble)));
  return Screen::status(format_elapsed(std::max(0, static_cast<int>(elapsed))),
                        missing);
}

} // namespace

etl::string<7> format_elapsed(int minutes) {
  etl::string<7> s;
  if (minutes < 60) { // "5m"
    etl::to_string(minutes, s);
    s += 'm';
  } else if (minutes < 180) { // "1h05"
    etl::to_string(minutes / 60, s);
    s += 'h';
    etl::to_string(minutes % 60, s, etl::format_spec().width(2).fill('0'),
                   true);
  } else { // "3h": whole hours, minutes dropped as on the Pi
    etl::to_string(minutes / 60, s);
    s += 'h';
  }
  return s;
}

Result screen_from_daily(etl::string_view body) {
  cJSON *root = cJSON_ParseWithLength(body.data(), body.size());
  if (root == nullptr) {
    return bad_response(); // not JSON
  }
  // Split from screen_from_root so the tree is freed once on every path.
  Result result = screen_from_root(root);
  cJSON_Delete(root);
  return result;
}

} // namespace dash

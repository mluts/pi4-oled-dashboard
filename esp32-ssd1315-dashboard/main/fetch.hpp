// GET /api/v1/daily in a task of its own; fetches never overlap.
#pragma once

#include "esp_err.h"
#include "etl/delegate.h"
#include "etl/expected.h"

#include "status.hpp"

namespace dash::fetch {

using Result = etl::expected<Screen, FetchError>;

// Runs in the fetch task with each outcome.
using DoneCallback = etl::delegate<void(const Result &)>;

// Builds the URL, sets up the HTTP client and starts the task.
esp_err_t start(DoneCallback on_done);

// Asks for a fetch. Calls during a running fetch collapse into one more fetch
// right after it (a task notification, not a queue).
void now();

} // namespace dash::fetch

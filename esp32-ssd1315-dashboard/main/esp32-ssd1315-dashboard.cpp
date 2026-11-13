#include "esp_log.h"
#include "etl/error_handler.h"
#include "etl/expected.h"
#include "etl/string.h"

static const char *TAG = "dash";

namespace {

// Failed ETL_ASSERTs end up here (ETL_LOG_ERRORS in etl_profile.h).
void on_etl_error(const etl::exception &e)
{
    ESP_LOGE(TAG, "etl: %s (%s:%d)", e.what(), e.file_name(), e.line_number());
}

}  // namespace

extern "C" void app_main(void)
{
    etl::error_handler::set_callback<on_etl_error>();

    // TODO: replaced by the real boot sequence in task 2.2; here only to prove
    // the ETL component links.
    etl::expected<etl::string<16>, int> hello{etl::string<16>{"booting"}};
    ESP_LOGI(TAG, "%s", hello.value().c_str());
}

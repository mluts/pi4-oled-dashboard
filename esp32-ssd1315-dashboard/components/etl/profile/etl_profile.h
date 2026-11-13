// Project profile for the ETL. ETL finds this file through the include path and
// reads it before anything else; the compiler, C++ level and target are
// auto-detected, so only the error policy is set here.
#pragma once

// NOTE: exceptions are off project-wide, so failed ETL_ASSERTs would be compiled
// out. With ETL_LOG_ERRORS they call the etl::error_handler callback instead,
// which app_main points at ESP_LOGE.
#define ETL_LOG_ERRORS
#define ETL_VERBOSE_ERRORS
// Bounds-check push/pop on fixed-capacity containers.
#define ETL_CHECK_PUSH_POP

idf_component_register(SRCS "esp32-ssd1315-dashboard.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES etl
                       PRIV_REQUIRES esp_driver_i2c esp_timer)

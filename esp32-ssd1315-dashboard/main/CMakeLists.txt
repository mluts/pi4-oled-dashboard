idf_component_register(SRCS "esp32-ssd1315-dashboard.cpp"
                            "draw.cpp"
                            "fetch.cpp"
                            "layout.cpp"
                            "net.cpp"
                            "screen.cpp"
                            "status.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES etl
                       PRIV_REQUIRES esp_driver_i2c esp_event esp_http_client
                                     esp_netif esp_timer esp_wifi mbedtls
                                     nvs_flash)

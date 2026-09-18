idf_component_register(SRCS "main.cpp" "hardware.cpp" "weather_client.cpp" "photo_client.cpp" "../third_party/XPowersLib/XPowersLibInterface.cpp" INCLUDE_DIRS "." PRIV_INCLUDE_DIRS "../third_party/XPowersLib" REQUIRES frame weather esp_http_client mbedtls esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_sdmmc fatfs esp_timer esp_wifi esp_event esp_netif nvs_flash json lwip)

target_compile_definitions(${COMPONENT_LIB} PRIVATE XPOWERS_CHIP_AXP2101 CONFIG_XPOWERS_ESP_IDF_NEW_API)

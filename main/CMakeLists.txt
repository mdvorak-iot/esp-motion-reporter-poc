cmake_minimum_required(VERSION 3.15.0)

if (CONFIG_BT_ENABLED)
    list(APPEND OPTIONAL_REQUIRES bt)
endif ()

idf_component_register(
        SRC_DIRS .
        INCLUDE_DIRS .
        REQUIRES
        freertos
        nvs_flash
        log
        esp32
        esp_wifi
        app_update
        wifi_provisioning
        ${OPTIONAL_REQUIRES}
        double_reset
        status_led
        wifi_reconnect
        mpu9250
)

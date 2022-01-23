cmake_minimum_required(VERSION 3.15.0)

if (CONFIG_BT_ENABLED)
    list(APPEND OPTIONAL_REQUIRES bt)
endif ()

idf_component_register(
        SRCS
        app_main.c
#        app_motion.c
        app_status.c
        util_append.c

        INCLUDE_DIRS .

        REQUIRES
        ${OPTIONAL_REQUIRES}
        freertos
        nvs_flash
        esp_websocket_client
        button
        status_led
        wifi_reconnect
        wifi_auto_prov
        u8g2
        u8g2_hal
#        mpu9250
#        ahrs
)

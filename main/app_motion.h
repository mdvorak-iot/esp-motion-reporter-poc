#pragma once

#include <esp_websocket_client.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t motion_sensors_init();

void motion_sensors_loop(esp_websocket_client_handle_t client);

#ifdef __cplusplus
}
#endif

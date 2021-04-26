#pragma once

#include <esp_websocket_client.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STATUS_LED_CONNECTING_INTERVAL 500
#define STATUS_LED_PROV_INTERVAL 50
#define STATUS_LED_READY_INTERVAL 100

void app_status_init(esp_websocket_client_handle_t client);

#ifdef __cplusplus
}
#endif

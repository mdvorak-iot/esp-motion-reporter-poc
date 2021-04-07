#include "device_state.h"
#include <esp_log.h>
#include <esp_wifi.h>
#include <wifi_reconnect.h>
#include <wps_config.h>

static const char TAG[] = "setup_wifi";

static void wifi_wps_event_handler(__unused void *arg, esp_event_base_t event_base,
                                   int32_t event_id, __unused void *event_data)
{
    if (event_base == WIFI_EVENT
        && (event_id == WIFI_EVENT_STA_WPS_ER_SUCCESS || event_id == WIFI_EVENT_STA_WPS_ER_TIMEOUT || event_id == WIFI_EVENT_STA_WPS_ER_FAILED))
    {
        wifi_reconnect_resume();
    }
}

void setup_wifi(const char *hostname)
{
    // Initialize WiFi
    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, hostname));

    ESP_ERROR_CHECK(wifi_reconnect_start()); // NOTE this must be called before connect, otherwise it might miss connected event

    // Resume on WPS end
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_wps_event_handler, NULL, NULL));
}

void setup_wifi_start()
{
    // Start WPS if WiFi is not configured, or reconfiguration was requested
    bool reconfigure = (xEventGroupGetBits(device_state) & STATE_BIT_RECONFIGURE) != 0;
    if (!wifi_reconnect_is_ssid_stored() || reconfigure)
    {
        // No WiFi
        ESP_LOGI(TAG, "reconfigure request detected, starting WPS");
        ESP_ERROR_CHECK(wps_config_start());
    }
    else
    {
        // Connect now
        wifi_reconnect_resume();
    }
}

#include <esp_event.h>
#include <esp_log.h>
#include <esp_websocket_client.h>
#include <freertos/event_groups.h>
#include <status_led.h>
#include <wifi_provisioning/manager.h>

static const char TAG[] = "app_status";

static const uint32_t STATUS_LED_CONNECTING_INTERVAL = 500u;
static const uint32_t STATUS_LED_PROV_INTERVAL = 50u;
static const uint32_t STATUS_LED_READY_INTERVAL = 100u;

static EventGroupHandle_t event_group = NULL;

#define WS_CONNECTED_BIT BIT0

static void connected_handler(__unused void *handler_arg, __unused esp_event_base_t event_base,
                              __unused int32_t event_id, __unused void *event_data)
{
    // State
    xEventGroupSetBits(event_group, WS_CONNECTED_BIT);

    // LED
    status_led_handle_ptr status_led = (status_led_handle_ptr)handler_arg;
    uint32_t timeout_ms = STATUS_LED_READY_INTERVAL * 6;
    status_led_set_interval_for(status_led, STATUS_LED_READY_INTERVAL, false, timeout_ms, false);
}

static void disconnected_handler(__unused void *handler_arg, __unused esp_event_base_t event_base,
                                 __unused int32_t event_id, __unused void *event_data)
{
    // State
    xEventGroupClearBits(event_group, WS_CONNECTED_BIT);

    // LED
    status_led_handle_ptr status_led = (status_led_handle_ptr)handler_arg;
    status_led_set_interval(status_led, STATUS_LED_CONNECTING_INTERVAL, true);
}

static void wifi_prov_handler(__unused void *handler_arg, __unused esp_event_base_t event_base,
                              int32_t event_id, __unused void *event_data)
{
    status_led_handle_ptr status_led = (status_led_handle_ptr)handler_arg;

    if (event_id == WIFI_PROV_START)
    {
        status_led_set_interval(status_led, STATUS_LED_PROV_INTERVAL, true);
    }
    else if (event_id == WIFI_PROV_END)
    {
        // If not already connected
        if ((xEventGroupGetBits(event_group) & WS_CONNECTED_BIT) == 0)
        {
            status_led_set_interval(status_led, STATUS_LED_CONNECTING_INTERVAL, true);
        }
    }
}

void app_status_init(esp_websocket_client_handle_t client)
{
    event_group = xEventGroupCreate();

    // Status LED
    esp_err_t err = status_led_create_default();
    if (err == ESP_OK)
    {
        // Start flashing
        ESP_ERROR_CHECK_WITHOUT_ABORT(status_led_set_interval(STATUS_LED_DEFAULT, STATUS_LED_CONNECTING_INTERVAL, true));

        // Register event handlers
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, wifi_prov_handler, STATUS_LED_DEFAULT, NULL));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, disconnected_handler, STATUS_LED_DEFAULT, NULL));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_websocket_register_events(client, WEBSOCKET_EVENT_DISCONNECTED, disconnected_handler, STATUS_LED_DEFAULT));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_websocket_register_events(client, WEBSOCKET_EVENT_CONNECTED, connected_handler, STATUS_LED_DEFAULT));
    }
    else
    {
        ESP_LOGE(TAG, "failed to create status_led on pin %d: %d %s", STATUS_LED_DEFAULT_GPIO, err, esp_err_to_name(err));
    }
}

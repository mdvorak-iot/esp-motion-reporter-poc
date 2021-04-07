#include <esp_event.h>
#include <esp_log.h>
#include <status_led.h>

static const char TAG[] = "setup_status_led";

static const uint32_t STATUS_LED_CONNECTING_INTERVAL = 500u;
static const uint32_t STATUS_LED_READY_INTERVAL = 100u;
static const uint32_t STATUS_LED_READY_COUNT = 3;

static void connected_handler(__unused void *handler_arg, __unused esp_event_base_t event_base,
                              __unused int32_t event_id, __unused void *event_data)
{
    status_led_handle_ptr status_led = (status_led_handle_ptr)handler_arg;

    uint32_t timeout_ms = STATUS_LED_READY_INTERVAL * (STATUS_LED_READY_COUNT * 2 + 1);
    status_led_set_interval_for(status_led, STATUS_LED_READY_INTERVAL, false, timeout_ms, false);
}

static void disconnected_handler(__unused void *handler_arg, __unused esp_event_base_t event_base,
                                 __unused int32_t event_id, __unused void *event_data)
{
    status_led_handle_ptr status_led = (status_led_handle_ptr)handler_arg;
    status_led_set_interval(status_led, STATUS_LED_CONNECTING_INTERVAL, true);
}

void setup_status_led()
{
    // Status LED
    esp_err_t err = status_led_create_default();
    if (err == ESP_OK)
    {
        ESP_ERROR_CHECK_WITHOUT_ABORT(status_led_set_interval(STATUS_LED_DEFAULT, STATUS_LED_CONNECTING_INTERVAL, true));

        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, connected_handler, STATUS_LED_DEFAULT, NULL));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, disconnected_handler, STATUS_LED_DEFAULT, NULL));

        // Start flashing
        status_led_set_interval(STATUS_LED_DEFAULT, STATUS_LED_CONNECTING_INTERVAL, true);
    }
    else
    {
        ESP_LOGE(TAG, "failed to create status_led on pin %d: %d %s", STATUS_LED_DEFAULT_GPIO, err, esp_err_to_name(err));
    }
}

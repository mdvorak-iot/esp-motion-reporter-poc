#include <esp_event.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <nvs_flash.h>
#include <status_led.h>

static const char TAG[] = "main";

// Global state
static status_led_handle_ptr status_led = nullptr;

static void setup_init();
static void setup_devices();
extern "C" void setup_wifi(const char *hostname);
extern "C" void setup_wifi_connect();
extern "C" void setup_status_led(gpio_num_t status_led_pin, bool status_led_on_state, status_led_handle_ptr *out_status_led);

extern "C" void app_main()
{
    esp_app_desc_t app_info = {};
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_get_partition_description(esp_ota_get_running_partition(), &app_info));

    // Setup
    setup_init();
    setup_devices();
    setup_wifi(app_info.project_name);
    setup_status_led(GPIO_NUM_6, true, &status_led);
    setup_wifi_connect(); // this should be last
    ESP_LOGI(TAG, "started %s %s", app_info.project_name, app_info.version);

    // Run
    ESP_LOGI(TAG, "life is good");
}

// Setup logic
static void setup_init()
{
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // System services
    ESP_ERROR_CHECK(esp_event_loop_create_default());
}

static void setup_devices()
{
    // Custom devices and other init, that needs to be done before waiting for wifi connection
}

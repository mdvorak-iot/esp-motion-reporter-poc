#include "device_state.h"
#include "hw_config.h"
#include <aws_iot_shadow.h>
#include <double_reset.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <mqtt_client.h>
#include <nvs_flash.h>
#include <status_led.h>

static const char TAG[] = "main";

// Global state
static hw_config config = {};
static status_led_handle_ptr status_led = nullptr;
static esp_mqtt_client_handle_t mqtt_client = nullptr;
static aws_iot_shadow_handle_ptr shadow_handle = nullptr;

static void setup_init();
static void setup_devices();
extern "C" void setup_wifi(const char *hostname);
extern "C" void setup_wifi_start();
extern "C" void setup_aws_iot(esp_mqtt_client_handle_t *out_mqtt_client, aws_iot_shadow_handle_ptr *out_shadow_client);
extern "C" void setup_status_led(gpio_num_t status_led_pin, bool status_led_on_state, aws_iot_shadow_handle_ptr shadow_handle, status_led_handle_ptr *out_status_led);

extern "C" void app_main()
{
    esp_app_desc_t app_info = {};
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_get_partition_description(esp_ota_get_running_partition(), &app_info));

    // Setup
    setup_init();
    setup_devices();
    setup_wifi(app_info.project_name);
    setup_aws_iot(&mqtt_client, &shadow_handle);
    setup_status_led(config.status_led_pin, config.status_led_on_state, shadow_handle, &status_led);
    setup_wifi_start(); // this should be last
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
    device_state_init();

    // Check double reset
    // NOTE this should be called as soon as possible, ideally right after nvs init
    bool reconfigure = false;
    ESP_ERROR_CHECK_WITHOUT_ABORT(double_reset_start(&reconfigure, DOUBLE_RESET_DEFAULT_TIMEOUT));
    if (reconfigure)
    {
        xEventGroupSetBits(device_state, STATE_BIT_RECONFIGURE);
    }

    // Load config
    err = config.load();
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGE(TAG, "failed to load config: %d %s", err, esp_err_to_name(err));
    }
}

static void setup_devices()
{
    // Custom devices and other init, that needs to be done before waiting for wifi connection
}

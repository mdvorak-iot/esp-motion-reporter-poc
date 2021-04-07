#include <double_reset.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <nvs_flash.h>

extern "C" {
#include <mpu9250.h>
#include <calibrate.h>
}

static const char TAG[] = "main";

// Global state
static bool reconfigure = false;

// Setup
static void setup_init();
static void setup_devices();
extern "C" void setup_wifi(const char *hostname);
extern "C" void setup_wifi_connect(bool reconfigure);
extern "C" void setup_status_led();

extern "C" void app_main()
{
    esp_app_desc_t app_info = {};
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_get_partition_description(esp_ota_get_running_partition(), &app_info));

    // Setup
    setup_init();
    setup_devices();
    setup_wifi(app_info.project_name);
    setup_status_led();
    setup_wifi_connect(reconfigure); // this should be last
    ESP_LOGI(TAG, "started %s %s", app_info.project_name, app_info.version);

    // Run
    // TODO
    calibrate_gyro();
    calibrate_accel();
    calibrate_mag();
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

    // Check double reset
    // NOTE this should be called as soon as possible, ideally right after nvs init
    ESP_ERROR_CHECK_WITHOUT_ABORT(double_reset_start(&reconfigure, DOUBLE_RESET_DEFAULT_TIMEOUT));
}

static void setup_devices()
{
    // TODO
}

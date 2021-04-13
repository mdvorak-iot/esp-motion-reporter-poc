#include <MPU.hpp>
#include <app_wifi.h>
#include <double_reset.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
#include <mpu/math.hpp>
#include <mpu/types.hpp>
#include <nvs_flash.h>
#include <wifi_reconnect.h>

static const char TAG[] = "app_main";

extern "C" void app_status_init();

extern "C" void app_main()
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
    bool reconfigure = false;
    ESP_ERROR_CHECK_WITHOUT_ABORT(double_reset_start(&reconfigure, DOUBLE_RESET_DEFAULT_TIMEOUT));

    // App info
    esp_app_desc_t app_info = {};
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_get_partition_description(esp_ota_get_running_partition(), &app_info));

    // Setup
    app_status_init();

    struct app_wifi_config wifi_cfg = {
        .security = WIFI_PROV_SECURITY_1,
        .service_name = app_info.project_name,
        .pop = nullptr,
        .hostname = app_info.project_name,
        .wifi_connect = wifi_reconnect_resume,
    };
    ESP_ERROR_CHECK(app_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
    ESP_ERROR_CHECK(wifi_reconnect_start());

    // Devices
    ESP_ERROR_CHECK(i2c0.begin((gpio_num_t)CONFIG_I2C_MASTER_SDA, (gpio_num_t)CONFIG_I2C_MASTER_SCL, CONFIG_I2C_MASTER_FREQUENCY));
    MPU_t mpu;
    mpu.setBus(i2c0);

    // Start
    ESP_ERROR_CHECK(app_wifi_start(reconfigure));
    ESP_LOGI(TAG, "starting");

    // Run
    while ((err = mpu.testConnection()) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to connect to the MPU, error=%#X", err);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "MPU connection successful!");
}

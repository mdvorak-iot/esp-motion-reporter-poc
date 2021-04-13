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
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CONFIG_I2C_MASTER_SDA,
        .scl_io_num = CONFIG_I2C_MASTER_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed = CONFIG_I2C_MASTER_FREQUENCY,
        },
    };
    ESP_ERROR_CHECK(i2c_param_config(CONFIG_I2C_MASTER_PORT_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(CONFIG_I2C_MASTER_PORT_NUM, conf.mode, 0, 0, 0));

    MPU_t mpu;

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

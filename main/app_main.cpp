#include <MPU.hpp>
#include <app_wifi.h>
#include <double_reset.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_websocket_client.h>
#include <esp_wifi.h>
#include <hmc5883l.h>
#include <mpu/math.hpp>
#include <nvs_flash.h>
#include <wifi_reconnect.h>

static const char TAG[] = "app_main";

static esp_websocket_client_handle_t client = nullptr;
static hmc5883l_dev_t hmc5883l = {};

extern "C" void app_status_init(esp_websocket_client_handle_t client);

static void websocket_event_handler(__unused void *handler_args, __unused esp_event_base_t base,
                                    int32_t event_id, void *event_data)
{
    auto *data = (esp_websocket_event_data_t *)event_data;
    if (event_id == WEBSOCKET_EVENT_DATA && data->op_code == 0x1)
    {
        ESP_LOGI(TAG, "received data: %.*s", data->data_len, data->data_ptr);
    }
}

template<class T>
T axis_cmp(const mpud::axes_t<T> &a, const mpud::axes_t<T> &b)
{
    T max = abs(a.x - b.x);
    T v = abs(a.y - b.y);
    if (v > max) max = v;
    v = abs(a.z - b.z);
    if (v > max) max = v;
    return max;
}

extern "C" [[noreturn]] void app_main()
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

    // WebSocket client
    esp_websocket_client_config_t websocket_cfg = {};
    websocket_cfg.uri = CONFIG_WEBSOCKET_SERVER_URL;

    client = esp_websocket_client_init(&websocket_cfg);
    ESP_ERROR_CHECK(esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, nullptr));

    // Status
    app_status_init(client);

    // WiFi
    struct app_wifi_config wifi_cfg = {
        .security = WIFI_PROV_SECURITY_0,
        .service_name = app_info.project_name,
        .pop = nullptr,
        .hostname = app_info.project_name,
        .wifi_connect = wifi_reconnect_resume,
    };
    ESP_ERROR_CHECK(app_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
    ESP_ERROR_CHECK(wifi_reconnect_start());

    // Devices
    // TODO special LED status while waiting for devices

    ESP_ERROR_CHECK(hmc5883l_init_desc(&hmc5883l, I2C_NUM_0, (gpio_num_t)CONFIG_I2C_MASTER_SDA, (gpio_num_t)CONFIG_I2C_MASTER_SCL));
    // NOTE this must be first, since it force-initializes I2C
    while ((err = hmc5883l_init(&hmc5883l)) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to connect to the HMC5883L, error=%#X %s", err, esp_err_to_name(err));
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    MPU_t mpu;
    mpu.setBus(i2c0);

    while ((err = mpu.testConnection()) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to connect to the MPU, error=%#X %s", err, esp_err_to_name(err));
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "MPU connection successful!");
    ESP_ERROR_CHECK(mpu.initialize());
    ESP_ERROR_CHECK(mpu.setSampleRate(1000));

    // Start
    ESP_ERROR_CHECK(app_wifi_start(reconfigure));
    ESP_LOGI(TAG, "starting");

    // Wait for WiFi
    wifi_reconnect_wait_for_connection(CONFIG_APP_WIFI_PROV_TIMEOUT_S * 1000 + CONFIG_WIFI_RECONNECT_CONNECT_TIMEOUT_MS);

    ESP_ERROR_CHECK(esp_websocket_client_start(client));
    while (!esp_websocket_client_is_connected(client))
    {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    // Calibrate
    // TODO calibration data persistence
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    mpud::raw_axes_t accelCalib;         // x, y, z axes as int16
    mpud::raw_axes_t gyroCalib;          // x, y, z axes as int16
    mpu.motion(&accelCalib, &gyroCalib); // read both in one shot

    // Report loop
    mpud::raw_axes_t accelRaw;  // x, y, z axes as int16
    mpud::raw_axes_t gyroRaw;   // x, y, z axes as int16
    mpud::float_axes_t accelG;  // accel axes in (g) gravity format
    mpud::float_axes_t gyroDPS; // gyro axes in (DPS) º/s format
    hmc5883l_data_t mag;        // magnetic sensor

    // Last reported
    mpud::float_axes_t accelLast; // x, y, z axes as int16
    mpud::float_axes_t gyroLast;  // x, y, z axes as int16

    char json[1024] = {};

    TickType_t start = xTaskGetTickCount();
    while (true)
    {
        // Throttle
        vTaskDelayUntil(&start, CONFIG_APP_REPORT_INTERVAL / portTICK_PERIOD_MS);

        // Read
        err = mpu.motion(&accelRaw, &gyroRaw);
        if (err != ESP_OK) ESP_LOGW(TAG, "failed to read mpu: %d %s", err, esp_err_to_name(err));

        err = hmc5883l_get_data(&hmc5883l, &mag);
        if (err != ESP_OK) ESP_LOGW(TAG, "failed to read hmc5883l: %d %s", err, esp_err_to_name(err));

        // Adjust according to calibration
        accelRaw.x = (int16_t)(accelRaw.x - accelCalib.x);
        accelRaw.y = (int16_t)(accelRaw.y - accelCalib.y);
        accelRaw.z = (int16_t)(accelRaw.z - accelCalib.z);
        gyroRaw.x = (int16_t)(gyroRaw.x - gyroCalib.x);
        gyroRaw.y = (int16_t)(gyroRaw.y - gyroCalib.y);
        gyroRaw.z = (int16_t)(gyroRaw.z - gyroCalib.z);

        // Convert
        accelG = mpud::accelGravity(accelRaw, mpud::ACCEL_FS_4G);
        gyroDPS = mpud::gyroDegPerSec(gyroRaw, mpud::GYRO_FS_500DPS);

        // TODO data smoothing

        // Compare
        if (axis_cmp(accelG, accelLast) < 0.05f
            && axis_cmp(gyroDPS, gyroLast) < 0.5f)
        {
            continue;
        }

        // Store
        accelLast = accelG;
        gyroLast = gyroDPS;

        // Debug
        ESP_LOGI(TAG, "accel: [%+6.2f %+6.2f %+6.2f ] (G)\tgyro: [%+7.2f %+7.2f %+7.2f ] (º/s)\tmag: [%7.2f %7.2f %7.2f]", accelG.x, accelG.y, accelG.z, gyroDPS.x, gyroDPS.y, gyroDPS.z, mag.x, mag.y, mag.z);

        // Report
        if (esp_websocket_client_is_connected(client))
        {
            int len = snprintf(json, sizeof(json), R"~({"a":[%.3f,%.3f,%.3f],"g":[%.3f,%.3f,%.3f],"m":[%.3f,%.3f,%.3f]})~", accelG.x, accelG.y, accelG.z, gyroDPS.x, gyroDPS.y, gyroDPS.z, mag.x, mag.y, mag.z);
            if (len >= 0 && len < sizeof(json))
            {
                esp_websocket_client_send_text(client, json, len, 1000 / portTICK_PERIOD_MS);
            }
            else
            {
                ESP_LOGE(TAG, "json json overflow, needed %d bytes", len);
            }
        }
    }
}

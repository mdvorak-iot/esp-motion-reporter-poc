#include "app_motion.h"
#include "app_status.h"
#include "sdkconfig.h"
#include <button.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include <esp_websocket_client.h>
#include <esp_wifi.h>
#include <math.h>
#include <nvs_flash.h>
#include <u8g2_esp32_hal.h>
#include <u8x8.h>
#include <wifi_auto_prov.h>
#include <wifi_reconnect.h>

static const char TAG[] = "app_main";

#define APP_DEVICE_NAME CONFIG_APP_DEVICE_NAME
#define APP_CONTROL_BUTTON_PIN CONFIG_APP_CONTROL_BUTTON_PIN
#define APP_CONTROL_BUTTON_PROVISION_MS CONFIG_APP_CONTROL_BUTTON_PROVISION_MS
#define APP_CONTROL_BUTTON_FACTORY_RESET_MS CONFIG_APP_CONTROL_BUTTON_FACTORY_RESET_MS
#define APP_SNTP_SERVER CONFIG_APP_SNTP_SERVER
#define APP_WEBSOCKET_SERVER_URL CONFIG_APP_WEBSOCKET_SERVER_URL
// TODO
//#define SAMPLE_INTERVAL_MS (1000 / CONFIG_SAMPLE_RATE_Hz) // Sample interval in milliseconds
#define SAMPLE_INTERVAL_MS (1000 / 30) // Sample interval in milliseconds

static RTC_DATA_ATTR bool force_provisioning = false;
static u8x8_t display = {};
static esp_websocket_client_handle_t client = NULL;

static void app_disconnect()
{
    // NOTE disconnect from any services, e.g. MQTT server
    wifi_reconnect_pause();
    esp_wifi_disconnect();
    // Slight delay for any async processes to finish
    vTaskDelay(100 / portTICK_PERIOD_MS);
}

static void control_button_handler(__unused void *arg, const struct button_data *data)
{
    if (data->event == BUTTON_EVENT_PRESSED && data->long_press)
    {
        // Factory reset
        ESP_LOGW(TAG, "user requested factory reset");
        app_disconnect();
        ESP_LOGW(TAG, "erase nvs");
        nvs_flash_deinit();
        nvs_flash_erase();
        esp_restart();
    }
    else if (data->event == BUTTON_EVENT_RELEASED && data->press_length_ms > APP_CONTROL_BUTTON_PROVISION_MS && !data->long_press)
    {
        // Provision Wi-Fi
        ESP_LOGW(TAG, "user requested wifi provisioning");
        app_disconnect();
        // Use RTC memory and deep sleep, to restart main cpu with provisioning flag set to true
        force_provisioning = true;
        esp_deep_sleep(1000);
    }
    else if (data->event == BUTTON_EVENT_RELEASED)
    {
        // App action
        ESP_LOGW(TAG, "user click");
        // TODO
    }
}

static void setup_sntp()
{
#if LWIP_DHCP_GET_NTP_SRV
    sntp_servermode_dhcp(1); // accept NTP offers from DHCP server, if any
#endif
#if LWIP_DHCP_GET_NTP_SRV && SNTP_MAX_SERVERS > 1
    sntp_setservername(1, APP_SNTP_SERVER);
#else
    // otherwise, use DNS address from a pool
    sntp_setservername(0, APP_SNTP_SERVER);
#endif

    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
    sntp_init();
}

void setup()
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
    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    // Provisioning mode
    bool provision_now = force_provisioning;
    force_provisioning = false;

    // Setup control button
    struct button_config control_btn_cfg = {
        .level = BUTTON_LEVEL_LOW_ON_PRESS,
        .internal_pull = true,
        .long_press_ms = APP_CONTROL_BUTTON_FACTORY_RESET_MS,
        .continuous_callback = false,
        .on_press = control_button_handler,
        .on_release = control_button_handler,
        .arg = NULL,
    };
    ESP_ERROR_CHECK(button_config(APP_CONTROL_BUTTON_PIN, &control_btn_cfg, NULL));

    // Setup status LED
    app_status_init(client);

    // Display
    // TODO Kconfig
    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
    u8g2_esp32_hal.i2c_num = I2C_NUM_1;
    u8g2_esp32_hal.sda = 19;
    u8g2_esp32_hal.scl = 22;
    u8g2_esp32_hal_init(u8g2_esp32_hal);

    u8x8_Setup(&display, u8x8_d_ssd1306_128x64_noname, u8x8_cad_ssd13xx_i2c, u8g2_esp32_i2c_byte_cb, u8g2_esp32_gpio_and_delay_cb);
    u8x8_SetI2CAddress(&display, 0x3C << 1);
    u8x8_InitDisplay(&display);
    u8x8_SetPowerSave(&display, false);
    u8x8_ClearDisplay(&display);

    // TODO
    u8x8_SetFont(&display, u8x8_font_5x8_r);
    u8x8_DrawString(&display, 0, 0, "FOOBAR");

    // Setup Wi-Fi
    char name[WIFI_AUTO_PROV_SERVICE_NAME_LEN] = {};
    ESP_ERROR_CHECK(wifi_auto_prov_generate_name(name, sizeof(name), APP_DEVICE_NAME, false));

    struct wifi_auto_prov_config wifi_cfg = {
        .security = WIFI_PROV_SECURITY_1,
        .service_name = name,
        .pop = NULL,
        .wifi_connect = wifi_reconnect_resume,
    };
    ESP_ERROR_CHECK(wifi_auto_prov_print_qr_code_handler_register(NULL));
    ESP_ERROR_CHECK(wifi_auto_prov_init(&wifi_cfg));
    ESP_ERROR_CHECK(tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, name));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
    ESP_ERROR_CHECK(wifi_reconnect_start());

    // Setup SNTP
    setup_sntp();

    // WebSocket client
    esp_websocket_client_config_t websocket_cfg = {};
    websocket_cfg.uri = APP_WEBSOCKET_SERVER_URL;

    client = esp_websocket_client_init(&websocket_cfg);

    // Start
    ESP_ERROR_CHECK(wifi_auto_prov_start(provision_now));
}

_Noreturn void app_main()
{
    setup();

    ESP_LOGI(TAG, "starting");

    // Devices
    // TODO
    //    esp_err_t err = motion_sensors_init();
    //    if (err != ESP_OK)
    //    {
    //        ESP_LOGE(TAG, "failed to initialize mpu: %d %s", err, esp_err_to_name(err));
    //    }

    // Wait for Wi-Fi
    wifi_reconnect_wait_for_connection(CONFIG_APP_WIFI_PROV_TIMEOUT_S * 1000 + CONFIG_WIFI_RECONNECT_CONNECT_TIMEOUT_MS);

    ESP_ERROR_CHECK(esp_websocket_client_start(client));
    // while (!esp_websocket_client_is_connected(client))
    // {
    //     vTaskDelay(100 / portTICK_PERIOD_MS);
    // }

    // Wait for NTP for 10 sec (after boot)
    ESP_LOGI(TAG, "waiting for NTP");
    while (time(NULL) < 10)
    {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    // Log start time
    {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        char now_str[30] = {};

        strftime(now_str, sizeof(now_str), "%x %X %Z", &tm);
        ESP_LOGI(TAG, "running %s", now_str);
    }

    // Report loop
    TickType_t last_collect = xTaskGetTickCount();

    while (true)
    {
        // Process sensors
        // TODO
        //motion_sensors_loop(client);

        // Throttle
        vTaskDelayUntil(&last_collect, SAMPLE_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}

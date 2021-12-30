#include "app_motion.h"
#include "app_status.h"
#include <app_wifi.h>
#include <button.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include <esp_websocket_client.h>
#include <esp_wifi.h>
#include <math.h>
#include <mpu9250.h>
#include <nvs_flash.h>
#include <status_led.h>
#include <wifi_reconnect.h>

static const char TAG[] = "app_main";

#define SAMPLE_INTERVAL_MS (1000 / CONFIG_SAMPLE_RATE_Hz) // Sample interval in milliseconds

static esp_websocket_client_handle_t client = NULL;
static RTC_DATA_ATTR bool provision = false;

// DEBUG
static void websocket_event_handler(__unused void *handler_args, __unused esp_event_base_t base,
                                    int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    if (event_id == WEBSOCKET_EVENT_DATA && data->op_code == 0x1)
    {
        ESP_LOGI(TAG, "received data: %.*s", data->data_len, data->data_ptr);
    }
}

static void provision_wifi()
{
    // Set RTC stored variable and deep sleep, which will essentially restart the code
    provision = true;
    esp_deep_sleep(1000);
}

static void control_button_handler(__unused void *arg, const struct button_data *data)
{
    if (data->event == BUTTON_EVENT_PRESSED && data->long_press)
    {
        // Calibrate
        // TODO
    }
    else if (data->event == BUTTON_EVENT_RELEASED && data->press_length_ms > 3000 && !data->long_press)
    {
        // Provision Wi-Fi
        provision_wifi();
    }
    else if (data->event == BUTTON_EVENT_RELEASED)
    {
        // Toggle display
        // TODO
    }
}

_Noreturn void app_main()
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

    // Control button
    // TODO config
    struct button_config control_btn_cfg = {
        .level = BUTTON_LEVEL_LOW_ON_PRESS,
        .internal_pull = false,
        .long_press_ms = 10000,
        .on_press = control_button_handler,
        .on_release = control_button_handler,
        .arg = NULL,
    };
    ESP_ERROR_CHECK(button_config(GPIO_NUM_0, &control_btn_cfg, NULL));

    // WebSocket client
    esp_websocket_client_config_t websocket_cfg = {};
    websocket_cfg.uri = CONFIG_WEBSOCKET_SERVER_URL;

    client = esp_websocket_client_init(&websocket_cfg);
    ESP_ERROR_CHECK(esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL));

    // Status
    app_status_init(client);

    // WiFi
    struct app_wifi_config wifi_cfg = {
        .security = WIFI_PROV_SECURITY_1,
        .service_name = NULL,
        .pop = NULL,
        .hostname = NULL,
        .wifi_connect = wifi_reconnect_resume,
    };
    ESP_ERROR_CHECK(app_wifi_print_qr_code_handler_register(NULL));
    ESP_ERROR_CHECK(app_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
    ESP_ERROR_CHECK(wifi_reconnect_start());

    // NTP
#if LWIP_DHCP_GET_NTP_SRV
    sntp_servermode_dhcp(1); // accept NTP offers from DHCP server, if any
#endif
#if LWIP_DHCP_GET_NTP_SRV && SNTP_MAX_SERVERS > 1
    sntp_setservername(1, "pool.ntp.org");
#else
    // otherwise, use DNS address from a pool
    sntp_setservername(0, "pool.ntp.org");
#endif

    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
    sntp_init();

    // Start
    ESP_ERROR_CHECK_WITHOUT_ABORT(status_led_set_interval(STATUS_LED_DEFAULT, STATUS_LED_CONNECTING_INTERVAL, true));
    ESP_ERROR_CHECK(app_wifi_start(provision));
    provision = false; // Reset RTC flag
    ESP_LOGI(TAG, "starting");

    // Devices
    err = motion_sensors_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "failed to initialize mpu: %d %s", err, esp_err_to_name(err));
    }

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
        motion_sensors_loop(client);

        // Throttle
        vTaskDelayUntil(&last_collect, SAMPLE_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}

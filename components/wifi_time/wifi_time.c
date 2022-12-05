#include "wifi_time.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_sntp.h"

#include "lwip/err.h"
#include "lwip/sys.h"

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_FAIL_BIT           BIT1
#define WIFI_DISCONNECTED_BIT   BIT2
#define WIFI_STOPPED_BIT        BIT3
#define SNTP_SUCCESS_BIT        BIT4
#define TIME_UPDATE_FAIL_BIT    BIT5
#define TIME_UPDATE_SUCCESS_BIT BIT6
#define LAST_EVENT_BIT          BIT6

#define WIFI_RETRIES 10
#define SNTP_RETRIES 10

static const char *TAG = "WIFI_TIME";

static int retry_count = 0;
static esp_event_handler_instance_t instance_any_id;
static esp_event_handler_instance_t instance_got_ip;
static esp_netif_t* netif_handle;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        ESP_LOGI(TAG, "Got wifi event: %d", event_id);
        
        switch(event_id) {
            case WIFI_EVENT_STA_START:
                ESP_ERROR_CHECK(esp_wifi_connect());
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                xEventGroupSetBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);
                break;
            case WIFI_EVENT_STA_STOP:
                xEventGroupSetBits(s_wifi_event_group, WIFI_STOPPED_BIT);
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

static void init_nvs() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

static void config_sntp() {
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    setenv("TZ", CONFIG_TIMEZONE, 1);
    tzset();
}

static void init_wifi() {
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    ESP_ERROR_CHECK(esp_netif_init());
    netif_handle = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {0};

    wifi_config.sta = (wifi_sta_config_t){
        .ssid = CONFIG_WIFI_SSID,
        .password = CONFIG_WIFI_PASSWORD,
        .threshold.authmode = WIFI_AUTH_WPA2_PSK,

        .pmf_cfg = {
            .capable = true,
            .required = false
        }
    };

    // wifi_config_t wifi_config = {
    //     .sta = {
    //         .ssid = CONFIG_WIFI_SSID,
    //         .password = CONFIG_WIFI_PASSWORD,
    //         /* Setting a password implies station will connect to all security modes including WEP/WPA.
    //          * However these modes are deprecated and not advisable to be used. Incase your Access point
    //          * doesn't support WPA2, these mode can be enabled by commenting below line */
	//         .threshold.authmode = WIFI_AUTH_WPA2_PSK,

    //         .pmf_cfg = {
    //             .capable = true,
    //             .required = false
    //         },
    //         .listen_interval = 5,
    //     },
    // };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
}

void wifi_time_init()
{
    init_nvs();
    init_wifi();
    config_sntp();
}

static void update_time_task(void *param) {
    xEventGroupClearBits(s_wifi_event_group, LAST_EVENT_BIT-1);

    retry_count = 0;

    // esp_wifi_restore();
    esp_err_t ret = esp_wifi_start();
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to connect to wifi: %d", ret);
        return;
    }


    while(true) {
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT|WIFI_DISCONNECTED_BIT, pdTRUE, pdFALSE, 500 / portTICK_PERIOD_MS);

        if(bits & WIFI_CONNECTED_BIT) {
            break;
        }

        esp_netif_dhcp_status_t status;
        esp_netif_dhcpc_get_status(netif_handle, &status);

        ESP_LOGI(TAG, "got dhcp status: %d", status);

        if (retry_count++ > WIFI_RETRIES) {
            ESP_LOGE(TAG, "failed to connect to wifi!");
            xEventGroupSetBits(s_wifi_event_group, TIME_UPDATE_FAIL_BIT);
            vTaskDelete(NULL);
            return;
        }

        esp_wifi_connect();
    }

    sntp_init();

    retry_count = 0;
    while(sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET || sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        ESP_LOGI(TAG, "Waiting for system time to be set");
        vTaskDelay(2000/portTICK_PERIOD_MS);
        if(retry_count++ > SNTP_RETRIES) {
            ESP_LOGE(TAG, "failed to get sntp time update!");
            xEventGroupSetBits(s_wifi_event_group, TIME_UPDATE_FAIL_BIT);
            vTaskDelete(NULL);
            return;
        }
    }

    sntp_stop();

    esp_wifi_disconnect();
    ESP_ERROR_CHECK(esp_wifi_stop());
    xEventGroupWaitBits(s_wifi_event_group, WIFI_STOPPED_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    ESP_LOGI(TAG, "wifi disconnect complete");
    // ESP_ERROR_CHECK(esp_wifi_deinit());
    // esp_netif_destroy(netif_handle);
    // esp_wifi_restore();



    char strftime_buf[64];
    struct tm timeinfo = {0};
    time_t now = 0;
    time(&now);

    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);

    ESP_LOGI(TAG, "Updated time to: %s", strftime_buf);
    xEventGroupSetBits(s_wifi_event_group, TIME_UPDATE_SUCCESS_BIT);

    vTaskDelay(100/portTICK_PERIOD_MS);

    vTaskDelete(NULL);
}

void update_time() {
    xTaskCreate(update_time_task, "update_time_task", 4096, NULL, 5, NULL);
}

esp_err_t blocking_update_time() {
    xTaskCreate(update_time_task, "update_time_task", 4096, NULL, 5, NULL);
    EventBits_t result = xEventGroupWaitBits(s_wifi_event_group, TIME_UPDATE_SUCCESS_BIT|TIME_UPDATE_FAIL_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

    return result & TIME_UPDATE_SUCCESS_BIT ? ESP_OK : ESP_FAIL;
}
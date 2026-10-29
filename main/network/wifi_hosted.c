#include "wifi_hosted.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_hosted.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "custom.h"

static const char *TAG = "WIFI_HOSTED";

/* ======================== 内部状态 ======================== */
static SemaphoreHandle_t s_hosted_up_sem  = NULL;
static bool s_transport_ready = false;
static bool s_wifi_connected  = false;
static bool s_manual_disconnect = false;  /* 用户主动断开，不自动重连 */
static bool s_auto_reconnect  = true;     /* 自动重连使能 */
static char s_connected_ssid[33] = "";    /* 当前连接的 SSID */
static char s_saved_password[64] = "";    /* 当前连接的密码（用于重连） */

/* 扫描回调 */
static wifi_scan_done_cb_t s_scan_cb = NULL;

/* WiFi 连接成功回调 */
static void (*s_connected_cb)(void) = NULL;

/* NVS 命名空间 */
#define NVS_NAMESPACE "wifi_cfg"
#define NVS_KEY_SSID  "ssid"
#define NVS_KEY_PASS  "pass"
#define NVS_KEY_AUTO  "auto"

/* ======================== ESP-Hosted 事件 ======================== */
static void hosted_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    if (event_base != ESP_HOSTED_EVENT) return;

    switch (event_id) {
    case ESP_HOSTED_EVENT_TRANSPORT_UP:
        ESP_LOGI(TAG, "Hosted Transport UP");
        s_transport_ready = true;
        if (s_hosted_up_sem) xSemaphoreGive(s_hosted_up_sem);
        break;
    case ESP_HOSTED_EVENT_CP_INIT:
        ESP_LOGI(TAG, "Co-processor INIT");
        break;
    case ESP_HOSTED_EVENT_TRANSPORT_DOWN:
        ESP_LOGW(TAG, "Hosted Transport DOWN");
        s_transport_ready = false;
        break;
    case ESP_HOSTED_EVENT_TRANSPORT_FAILURE:
        ESP_LOGE(TAG, "Hosted Transport FAILURE");
        s_transport_ready = false;
        break;
    default:
        break;
    }
}

/* ======================== 重连辅助任务 ======================== */
static void reconnect_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(3000));  /* 延迟 3 秒重连 */

    if (s_manual_disconnect || !s_auto_reconnect || strlen(s_connected_ssid) == 0) {
        ESP_LOGI(TAG, "Reconnect cancelled (manual=%d, auto=%d)",
                 s_manual_disconnect, s_auto_reconnect);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Auto-reconnecting to: %s", s_connected_ssid);
    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", s_connected_ssid);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", s_saved_password);

    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_connect();

    vTaskDelete(NULL);
}

/* ======================== WiFi / IP 事件 ======================== */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "Wi-Fi STA started");
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG, "Wi-Fi disconnected, reason=%d", disconn->reason);
            s_wifi_connected = false;
            custom_set_wifi_status(false);

            /* 非用户主动断开且启用自动重连 → 延迟重连 */
            if (!s_manual_disconnect && s_auto_reconnect && strlen(s_connected_ssid) > 0) {
                xTaskCreate(reconnect_task, "wifi_reconn", 4096, NULL, 3, NULL);
            }
            break;
        }

        case WIFI_EVENT_SCAN_DONE: {
            uint16_t ap_count = 0;
            esp_wifi_scan_get_ap_num(&ap_count);
            if (ap_count == 0) {
                if (s_scan_cb) s_scan_cb(NULL, 0);
                break;
            }

            wifi_ap_info_t *aps = malloc(sizeof(wifi_ap_info_t) * ap_count);
            if (!aps) {
                if (s_scan_cb) s_scan_cb(NULL, 0);
                break;
            }

            wifi_ap_record_t *records = malloc(sizeof(wifi_ap_record_t) * ap_count);
            if (!records) {
                free(aps);
                if (s_scan_cb) s_scan_cb(NULL, 0);
                break;
            }

            esp_wifi_scan_get_ap_records(&ap_count, records);

            for (int i = 0; i < ap_count; i++) {
                memset(aps[i].ssid, 0, sizeof(aps[i].ssid));
                size_t ssid_len = strnlen((const char *)records[i].ssid, 32);
                memcpy(aps[i].ssid, records[i].ssid, ssid_len);
                aps[i].ssid[ssid_len] = '\0';
                aps[i].rssi      = records[i].rssi;
                aps[i].auth_mode  = records[i].authmode;
                aps[i].channel   = records[i].primary;
            }

            free(records);

            if (s_scan_cb) s_scan_cb(aps, ap_count);
            free(aps);
            break;
        }

        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_connected = true;
        s_manual_disconnect = false;
        custom_set_wifi_status(true);
        /* 通知注册者（如 MQTT 初始化） */
        if (s_connected_cb) s_connected_cb();
    }
}

/* ======================== 公共接口 ======================== */

bool wifi_hosted_init_transport(uint32_t timeout_ms)
{
    /* --- 1. 初始化 NVS --- */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition corrupt, erasing...");
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* --- 2. 创建信号量 --- */
    s_hosted_up_sem = xSemaphoreCreateBinary();
    if (!s_hosted_up_sem) {
        ESP_LOGE(TAG, "Create semaphore failed");
        return false;
    }

    /* --- 3. 初始化 Netif + 默认事件循环 --- */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* --- 4. 注册事件 --- */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(ESP_HOSTED_EVENT,
        ESP_EVENT_ANY_ID, &hosted_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
        ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
        IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    /* --- 5. 初始化 ESP-Hosted --- */
    ESP_LOGI(TAG, "Initializing ESP-Hosted...");
    esp_hosted_init();
    esp_hosted_connect_to_slave();

    /* --- 6. 等待 Transport UP --- */
    ESP_LOGI(TAG, "Waiting for Hosted Transport...");
    TickType_t wait_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(s_hosted_up_sem, wait_ticks) != pdTRUE) {
        ESP_LOGE(TAG, "Hosted Transport timeout!");
        return false;
    }

    /* --- 7. 初始化 WiFi STA（不连接） --- */
    ESP_LOGI(TAG, "Initializing Wi-Fi STA...");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* 加载自动重连设置 */
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        int8_t auto_val = 1;
        nvs_get_i8(nvs, NVS_KEY_AUTO, &auto_val);
        s_auto_reconnect = (auto_val != 0);
        nvs_close(nvs);
    }

    ESP_LOGI(TAG, "Wi-Fi transport ready (auto_reconnect=%d)", s_auto_reconnect);
    return true;
}

bool wifi_hosted_scan_start(wifi_scan_done_cb_t callback)
{
    if (!s_transport_ready) {
        ESP_LOGW(TAG, "Transport not ready, cannot scan");
        return false;
    }

    s_scan_cb = callback;

    wifi_scan_config_t scan_conf = {
        .ssid        = NULL,
        .bssid       = NULL,
        .channel     = 0,
        .show_hidden = false,
        .scan_type   = WIFI_SCAN_TYPE_ACTIVE,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_conf, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(err));
        s_scan_cb = NULL;
        return false;
    }

    ESP_LOGI(TAG, "WiFi scan started");
    return true;
}

bool wifi_hosted_connect(const char *ssid, const char *password)
{
    if (!s_transport_ready) {
        ESP_LOGW(TAG, "Transport not ready, cannot connect");
        return false;
    }

    if (!ssid || strlen(ssid) == 0) return false;

    /* 记录连接目标（用于重连） */
    strncpy(s_connected_ssid, ssid, sizeof(s_connected_ssid) - 1);
    s_connected_ssid[sizeof(s_connected_ssid) - 1] = '\0';
    if (password) {
        strncpy(s_saved_password, password, sizeof(s_saved_password) - 1);
        s_saved_password[sizeof(s_saved_password) - 1] = '\0';
    } else {
        s_saved_password[0] = '\0';
    }
    s_manual_disconnect = false;

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Connect failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Connecting to: %s", ssid);
    return true;
}

void wifi_hosted_disconnect(void)
{
    s_manual_disconnect = true;
    s_wifi_connected = false;
    s_connected_ssid[0] = '\0';
    esp_wifi_disconnect();
    custom_set_wifi_status(false);
    ESP_LOGI(TAG, "Disconnected (manual)");
}

bool wifi_hosted_save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed");
        return false;
    }

    esp_err_t err = ESP_OK;
    err |= nvs_set_str(nvs, NVS_KEY_SSID, ssid ? ssid : "");
    err |= nvs_set_str(nvs, NVS_KEY_PASS, password ? password : "");
    err |= nvs_set_i8(nvs, NVS_KEY_AUTO, 1);
    err |= nvs_commit(nvs);
    nvs_close(nvs);

    if (err == ESP_OK) {
        s_auto_reconnect = true;
        ESP_LOGI(TAG, "Credentials saved: %s", ssid);
    }
    return err == ESP_OK;
}

bool wifi_hosted_load_credentials(char *ssid, size_t ssid_len,
                                   char *password, size_t pass_len)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }

    esp_err_t err = nvs_get_str(nvs, NVS_KEY_SSID, ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(nvs);
        return false;
    }

    err = nvs_get_str(nvs, NVS_KEY_PASS, password, &pass_len);
    nvs_close(nvs);

    return err == ESP_OK;
}

bool wifi_hosted_auto_connect(void)
{
    char ssid[33] = {0};
    char password[64] = {0};

    if (!wifi_hosted_load_credentials(ssid, sizeof(ssid), password, sizeof(password))) {
        ESP_LOGI(TAG, "No saved WiFi credentials");
        return false;
    }

    if (strlen(ssid) == 0) {
        ESP_LOGI(TAG, "Saved SSID is empty");
        return false;
    }

    ESP_LOGI(TAG, "Auto-connecting to saved: %s", ssid);
    return wifi_hosted_connect(ssid, password);
}

void wifi_hosted_clear_credentials(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_str(nvs, NVS_KEY_SSID, "");
        nvs_set_str(nvs, NVS_KEY_PASS, "");
        nvs_set_i8(nvs, NVS_KEY_AUTO, 0);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    s_auto_reconnect = false;
    s_connected_ssid[0] = '\0';
    s_saved_password[0] = '\0';
    ESP_LOGI(TAG, "WiFi credentials cleared");
}

bool wifi_is_connected(void)
{
    return s_wifi_connected;
}

const char *wifi_get_connected_ssid(void)
{
    return s_wifi_connected ? s_connected_ssid : NULL;
}

bool wifi_hosted_is_ready(void)
{
    return s_transport_ready;
}

void wifi_hosted_set_connected_cb(void (*cb)(void))
{
    s_connected_cb = cb;
}

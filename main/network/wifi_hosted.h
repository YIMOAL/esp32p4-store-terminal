#ifndef __WIFI_HOSTED_H
#define __WIFI_HOSTED_H

#include <stdbool.h>
#include "esp_log.h"
#include "esp_wifi.h"

/**
 * @brief WiFi AP 扫描结果信息
 */
typedef struct {
    char ssid[33];           /*!< SSID (最长32字符) */
    int8_t rssi;             /*!< 信号强度 */
    wifi_auth_mode_t auth_mode; /*!< 认证模式 */
    uint8_t channel;         /*!< 信道 */
} wifi_ap_info_t;

/**
 * @brief WiFi 扫描完成回调函数类型
 * @param aps     扫描结果数组（回调结束后由内部释放）
 * @param count   AP 数量
 */
typedef void (*wifi_scan_done_cb_t)(wifi_ap_info_t *aps, int count);

/**
 * @brief 初始化 ESP-Hosted 传输层 + WiFi STA（不自动连接）
 * @param timeout_ms 等待 Hosted Transport 就绪的超时时间(ms)，0=无限等待
 * @return true 成功，false 失败
 */
bool wifi_hosted_init_transport(uint32_t timeout_ms);

/**
 * @brief 启动 WiFi 扫描，结果通过回调返回
 * @param callback 扫描完成回调（在 WiFi 事件任务上下文调用）
 * @return true 扫描已启动，false 失败
 */
bool wifi_hosted_scan_start(wifi_scan_done_cb_t callback);

/**
 * @brief 连接到指定 WiFi 网络
 * @param ssid     SSID
 * @param password 密码（开放网络传 NULL 或空串）
 * @return true 连接请求已发起，false 失败
 */
bool wifi_hosted_connect(const char *ssid, const char *password);

/**
 * @brief 断开当前 WiFi 连接（用户主动断开，不自动重连）
 */
void wifi_hosted_disconnect(void);

/**
 * @brief 保存 WiFi 凭证到 NVS
 */
bool wifi_hosted_save_credentials(const char *ssid, const char *password);

/**
 * @brief 从 NVS 加载 WiFi 凭证
 * @return true 有保存的凭证，false 无
 */
bool wifi_hosted_load_credentials(char *ssid, size_t ssid_len,
                                   char *password, size_t pass_len);

/**
 * @brief 使用保存的凭证自动连接
 * @return true 已发起连接，false 无保存凭证
 */
bool wifi_hosted_auto_connect(void);

/**
 * @brief 清除 NVS 中保存的 WiFi 凭证（断开连接时使用）
 */
void wifi_hosted_clear_credentials(void);

/**
 * @brief 检查 WiFi 是否已连接并获取到 IP
 */
bool wifi_is_connected(void);

/**
 * @brief 获取当前连接的 SSID
 */
const char *wifi_get_connected_ssid(void);

/**
 * @brief 检查 WiFi 传输层是否就绪
 */
bool wifi_hosted_is_ready(void);

/**
 * @brief 注册 WiFi 连接成功（获取到 IP）时的回调
 * @param cb 回调函数（在 WiFi 事件任务上下文中调用）
 */
void wifi_hosted_set_connected_cb(void (*cb)(void));

#endif

#ifndef __MQTT_H
#define __MQTT_H

#include <stdbool.h>
#include "esp_log.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "app_config.h"

/**
 * @brief MQTT 响应回调函数类型
 * @param resp_json 响应的 cJSON 对象（调用者负责释放）
 */
typedef void (*mqtt_resp_cb_t)(cJSON *resp_json);

/**
 * @brief 启动MQTT客户端
 */
void mqtt_start(void);

/**
 * @brief 检查MQTT是否已连接
 */
bool mqtt_is_connected(void);

/**
 * @brief 发送MQTT请求并注册响应回调
 * @param action     动作名，如 "get_overview"
 * @param params     请求参数的 cJSON 对象（可为NULL）
 * @param callback   响应回调函数（可为NULL）
 * @return int       请求ID，-1 表示发送失败
 */
int mqtt_send_request(const char *action, cJSON *params, mqtt_resp_cb_t callback);

/**
 * @brief 取消待处理的响应回调
 * @param req_id 要取消的请求ID
 */
void mqtt_cancel_callback(int req_id);

#endif

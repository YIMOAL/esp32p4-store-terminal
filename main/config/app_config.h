#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

/**
 * @file app_config.h
 * @brief 应用配置总头文件
 * 
 * 敏感配置（WiFi密码、MQTT凭证等）请在 app_config_private.h 中定义。
 * 若 app_config_private.h 不存在，将使用以下默认值。
 */

/* ---- 尝试包含私有配置（覆盖默认值） ---- */
#if __has_include("app_config_private.h")
#include "app_config_private.h"
#endif

/* ======================== Wi-Fi 配置 ======================== */
#ifndef WIFI_SSID
#define WIFI_SSID      "YOUR_WIFI_SSID"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"
#endif

/* ======================== MQTT 配置 ======================== */
#ifndef MQTT_BROKER
#define MQTT_BROKER          "mqtt://broker.hivemq.com:1883"
#endif

#ifndef MQTT_PORT
#define MQTT_PORT            1883
#endif

#ifndef MQTT_CLIENT_ID
#define MQTT_CLIENT_ID       "esp32p4_clothing_client"
#endif

#ifndef MQTT_USER
#define MQTT_USER            ""
#endif

#ifndef MQTT_PASS
#define MQTT_PASS            ""
#endif

#ifndef MQTT_TOPIC_REQ
#define MQTT_TOPIC_REQ       "clothing/req"
#endif

#ifndef MQTT_TOPIC_RESP
#define MQTT_TOPIC_RESP      "clothing/resp"
#endif

#endif /* __APP_CONFIG_H */

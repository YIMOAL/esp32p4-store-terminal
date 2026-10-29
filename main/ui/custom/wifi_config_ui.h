#ifndef __WIFI_CONFIG_UI_H
#define __WIFI_CONFIG_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief 打开 WiFi 配置面板
 * @param parent 父对象（通常为 lv_layer_top()）
 */
void wifi_config_ui_open(void);

/**
 * @brief 关闭 WiFi 配置面板
 */
void wifi_config_ui_close(void);

/**
 * @brief 检查 WiFi 配置面板是否已打开
 */
bool wifi_config_ui_is_open(void);

/**
 * @brief 更新 WiFi 配置面板的连接状态显示
 *        由 wifi_hosted 事件回调调用
 */
void wifi_config_ui_update_status(void);

#ifdef __cplusplus
}
#endif

#endif /* __WIFI_CONFIG_UI_H */

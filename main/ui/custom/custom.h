/*
* Copyright 2024 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/


#ifndef __CUSTOM_H_
#define __CUSTOM_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "gui_guider.h"
#include "wifi_config_ui.h"

void custom_init(lv_ui *ui);

/* 页面进入回调（在页面加载后调用，注册事件+自动拉取数据） */
void on_enter_screen_stock(void);
void on_enter_screen_sell(void);
void on_enter_screen_check(void);
void on_enter_screen_set(void);

/* 数据加载函数 */
void ui_load_overview(void);
void ui_load_stock_records(const char *date, int page);
void ui_load_sell_records(const char *date, int page);
void ui_load_check_category(const char *category, int page);
void ui_search_items(const char *keyword, int page);

/* 状态指示器：WiFi/MQTT 连接状态 */
void custom_set_wifi_status(bool connected);
void custom_set_mqtt_status(bool connected);

/* 通知 SNTP 已在 main 中初始化（避免重复初始化） */
void custom_notify_sntp_inited(void);

#ifdef __cplusplus
}
#endif
#endif /* __CUSTOM_H_ */

/*
 * custom.c — 服装库存管理系统 业务逻辑
 * 负责：MQTT 请求发送、响应处理、UI 数据更新
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "lvgl.h"
#include "custom.h"
#include "gui_guider.h"
#include "events_init.h"
#include "network/mqtt.h"
#include "network/wifi_hosted.h"
#include "bsp/display_init.h"
#include "adapter/lvgl_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "wifi_config_ui.h"

/* 前向声明（定义在下方） */
static void ensure_sntp_init(void);

static const char *TAG = "custom";

/* ======================== 页面状态 ======================== */

/* 当前活跃页面（防止页面切换后回调操作已释放的控件） */
typedef enum {
    SCREEN_MAIN,
    SCREEN_STOCK,
    SCREEN_SELL,
    SCREEN_CHECK,
    SCREEN_SET
} active_screen_t;
static active_screen_t s_active_screen = SCREEN_MAIN;

/* 网络状态指示器 */
static bool s_wifi_connected = false;
static bool s_mqtt_connected = false;

/* 主屏概览 */
static int s_overview_req_id = -1;

/* 入库页面 */
static char s_stock_date[16] = "";     /* "2026-05-20" */
static int  s_stock_page = 1;
static int  s_stock_total_pages = 1;
static int  s_stock_req_id = -1;

/* 售货页面 */
static char s_sell_date[16] = "";
static int  s_sell_page = 1;
static int  s_sell_total_pages = 1;
static int  s_sell_req_id = -1;

/* 查看页面 */
static char s_check_category[16] = "上衣";
static int  s_check_page = 1;
static int  s_check_total_pages = 1;
static int  s_check_req_id = -1;
static char s_check_keyword[64] = "";

/* 每日同步：记录上次同步日期，0点时触发一次 */
static char s_last_sync_date[16] = "";

/* ======================== MQTT 异步请求队列 ======================== */
typedef enum {
    REQ_OVERVIEW,
    REQ_STOCK_RECORDS,
    REQ_SELL_RECORDS,
    REQ_CHECK_CATEGORY,
    REQ_SEARCH_ITEMS,
    REQ_ADD_STOCK_RECORD,
    REQ_ADD_STOCK_FULL,      /* 完整入库（含服装信息） */
    REQ_DELETE_STOCK_RECORD,
    REQ_DELETE_STOCK_BY_CODE,
    REQ_ADD_SELL_RECORD,
    REQ_ADD_SELL_FULL,       /* 完整售货（含服装信息+金额） */
    REQ_DELETE_SELL_RECORD,
    REQ_DELETE_SELL_BY_CODE,
    REQ_SYNC_SELL_RECORDS,
} req_type_t;

/* 新增/删除记录的参数结构 */
typedef struct {
    req_type_t type;
    char param1[64];   /* date / category / keyword */
    int  param2;       /* page / record_id */
    char param3[64];   /* size */
    char param4[32];   /* quantity */
    char param5[64];   /* code (款号) */
    char param6[64];   /* name (款式) */
    char param7[64];   /* brand (牌子) */
    char param8[64];   /* color (颜色) */
    char param9[32];   /* price (价格/金额) */
} req_msg_t;

static QueueHandle_t s_req_queue = NULL;
static bool post_request(req_type_t type, const char *param1, int param2);

/* 取消所有子页面的待处理请求 */
static void cancel_child_requests(void)
{
    if (s_stock_req_id > 0) { mqtt_cancel_callback(s_stock_req_id); s_stock_req_id = -1; }
    if (s_sell_req_id > 0)  { mqtt_cancel_callback(s_sell_req_id);  s_sell_req_id = -1; }
    if (s_check_req_id > 0) { mqtt_cancel_callback(s_check_req_id); s_check_req_id = -1; }
}

/* 更新状态栏 WiFi/MQTT 图标可见性（仅在主屏有效） */
static void update_status_icons(void)
{
    if (s_active_screen != SCREEN_MAIN) return;

    if (guider_ui.screen_img_4) {
        if (s_wifi_connected)
            lv_obj_clear_flag(guider_ui.screen_img_4, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(guider_ui.screen_img_4, LV_OBJ_FLAG_HIDDEN);
    }
    if (guider_ui.screen_img_5) {
        if (s_mqtt_connected)
            lv_obj_clear_flag(guider_ui.screen_img_5, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(guider_ui.screen_img_5, LV_OBJ_FLAG_HIDDEN);
    }
}

void custom_set_wifi_status(bool connected)
{
    s_wifi_connected = connected;
    /* UI 未初始化时只记录状态，等 custom_init 时再更新图标 */
    if (!guider_ui.screen_img_4) return;
    lvgl_lock();
    update_status_icons();
    /* 同步更新 WiFi 配置面板状态 */
    wifi_config_ui_update_status();
    lvgl_unlock();
}

void custom_set_mqtt_status(bool connected)
{
    s_mqtt_connected = connected;
    if (!guider_ui.screen_img_5) return;
    lvgl_lock();
    update_status_icons();
    /* MQTT 刚连接时如果在主屏，异步加载概览数据 */
    if (connected && s_active_screen == SCREEN_MAIN) {
        post_request(REQ_OVERVIEW, NULL, 0);
    }
    /* MQTT 连接后确保 SNTP 已初始化（处理网络先失败后恢复的情况） */
    if (connected) {
        ensure_sntp_init();
    }
    lvgl_unlock();
}

/* ======================== 工具函数 ======================== */

/* SNTP 时间同步标志 */
static bool s_sntp_synced = false;
static bool s_sntp_inited = false;

/* SNTP 同步回调 */
static void sntp_sync_cb(struct timeval *tv)
{
    s_sntp_synced = true;
    ESP_LOGI(TAG, "SNTP time synchronized");
}

/* 确保 SNTP 已初始化（可在 MQTT 连接后补初始化） */
static void ensure_sntp_init(void)
{
    if (s_sntp_inited) return;
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(sntp_sync_cb);
    esp_sntp_init();
    s_sntp_inited = true;
    ESP_LOGI(TAG, "SNTP initialized (late)");
}

/* 外部通知 SNTP 已在 main 中初始化 */
void custom_notify_sntp_inited(void)
{
    s_sntp_inited = true;
    esp_sntp_set_time_sync_notification_cb(sntp_sync_cb);
    ESP_LOGI(TAG, "SNTP notified as already initialized");
}

/* 获取今日日期字符串 "YYYY-MM-DD"（SNTP 优先，未同步则用系统时间） */
static void get_today_str(char *buf, size_t len)
{
    time_t now = 0;
    struct tm timeinfo = {0};
    time(&now);
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year < (2024 - 1900)) {
        /* SNTP 未同步，使用默认日期 */
        snprintf(buf, len, "2026-05-20");
    } else {
        snprintf(buf, len, "%04d-%02d-%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    }
}

/* 获取当前时间字符串 "HH:MM:SS" */
static void get_time_str(char *buf, size_t len)
{
    time_t now = 0;
    struct tm timeinfo = {0};
    time(&now);
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year < (2024 - 1900)) {
        snprintf(buf, len, "--:--:--");
    } else {
        snprintf(buf, len, "%02d:%02d:%02d",
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
}

/* 获取当前日期字符串 "YYYY/MM/DD"（用于日历控件格式） */
static void get_date_display_str(char *buf, size_t len)
{
    time_t now = 0;
    struct tm timeinfo = {0};
    time(&now);
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year < (2024 - 1900)) {
        snprintf(buf, len, "2026/05/20");
    } else {
        snprintf(buf, len, "%04d/%02d/%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    }
}

/* 在容器中清除动态子对象（保留静态控件） */
static void clear_dynamic_children(lv_obj_t *cont)
{
    uint32_t cnt = lv_obj_get_child_count(cont);
    for (int32_t i = cnt - 1; i >= 0; i--) {
        lv_obj_t *child = lv_obj_get_child(cont, i);
        /* 通过 user_data 标记动态创建的对象 */
        if (lv_obj_get_user_data(child) == (void *)1) {
            lv_obj_delete(child);
        }
    }
}

/* 标记对象为动态创建 */
static void mark_dynamic(lv_obj_t *obj)
{
    lv_obj_set_user_data(obj, (void *)1);
}

/* 安全获取 cJSON 中字符串 */
static const char *json_str(cJSON *obj, const char *key, const char *def)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsString(item)) return item->valuestring;
    return def;
}

/* 安全获取 cJSON 中整数 */
static int json_int(cJSON *obj, const char *key, int def)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsNumber(item)) return item->valueint;
    return def;
}

/* 安全获取 cJSON 中浮点数 */
static double json_double(cJSON *obj, const char *key, double def)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsNumber(item)) return item->valuedouble;
    return def;
}

/* ======================== 主屏概览 ======================== */

static void overview_resp_cb(cJSON *resp)
{
    cJSON *data = cJSON_GetObjectItem(resp, "data");
    if (!data) { cJSON_Delete(resp); return; }

    lvgl_lock();

    /* 当前不在主屏则跳过（控件可能已释放） */
    if (s_active_screen != SCREEN_MAIN) {
        lvgl_unlock();
        cJSON_Delete(resp);
        return;
    }

    /* 更新库存总量 */
    int total_stock = json_int(data, "total_stock", 0);
    char buf[64];
    snprintf(buf, sizeof(buf), "店内库存总量: %d", total_stock);
    lv_label_set_text(guider_ui.screen_label_6, buf);

    /* 更新今日销售额 */
    double today_sales = json_double(data, "today_sales", 0);
    snprintf(buf, sizeof(buf), "今日销售额: %.2f", today_sales);
    lv_label_set_text(guider_ui.screen_label_7, buf);

    lvgl_unlock();
    cJSON_Delete(resp);
}

void ui_load_overview(void)
{
    if (!mqtt_is_connected()) return;
    cancel_child_requests();
    s_active_screen = SCREEN_MAIN;
    update_status_icons();
    post_request(REQ_OVERVIEW, NULL, 0);
}

/* ======================== 入库记录 ======================== */

static void stock_records_resp_cb(cJSON *resp)
{
    cJSON *data = cJSON_GetObjectItem(resp, "data");
    if (!data) { cJSON_Delete(resp); return; }

    s_stock_page = json_int(data, "page", 1);
    s_stock_total_pages = json_int(data, "total_pages", 1);

	lvgl_lock();

	if (s_active_screen != SCREEN_STOCK) {
		lvgl_unlock();
		cJSON_Delete(resp);
		return;
	}

	/* 在主内容容器中创建表格 */
	lv_obj_t *cont = guider_ui.screen_stock_cont_1;
    if (!cont) { lvgl_unlock(); cJSON_Delete(resp); return; }

    /* 清除之前的动态内容 */
    clear_dynamic_children(cont);

    /* 表头 */
    const char *headers[] = {"款号", "款式", "牌子", "颜色", "尺码", "数量", "价格"};
    int col_x[] = {10, 130, 250, 370, 480, 580, 680};

    lv_obj_t *hdr_row = lv_obj_create(cont);
    mark_dynamic(hdr_row);
    lv_obj_set_size(hdr_row, 840, 35);
    lv_obj_set_pos(hdr_row, 10, 70);
    lv_obj_set_style_bg_color(hdr_row, lv_color_hex(0x2195f6), 0);
    lv_obj_set_style_bg_opa(hdr_row, 255, 0);
    lv_obj_set_style_border_width(hdr_row, 0, 0);
    lv_obj_set_style_radius(hdr_row, 0, 0);
    lv_obj_set_style_pad_all(hdr_row, 0, 0);

    for (int i = 0; i < 7; i++) {
        lv_obj_t *h = lv_label_create(hdr_row);
        mark_dynamic(h);
        lv_label_set_text(h, headers[i]);
        lv_obj_set_pos(h, col_x[i], 7);
        lv_obj_set_style_text_font(h, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
        lv_obj_set_style_text_color(h, lv_color_hex(0xffffff), 0);
    }

    /* 数据行 */
    cJSON *items = cJSON_GetObjectItem(data, "items");
    if (items && cJSON_IsArray(items)) {
        int row_count = cJSON_GetArraySize(items);
        for (int i = 0; i < row_count; i++) {
            cJSON *item = cJSON_GetArrayItem(items, i);
            int y = 110 + i * 42;

            lv_obj_t *row_bg = lv_obj_create(cont);
            mark_dynamic(row_bg);
            lv_obj_set_size(row_bg, 840, 38);
            lv_obj_set_pos(row_bg, 10, y);
            lv_obj_set_style_bg_color(row_bg, lv_color_hex(i % 2 ? 0xf5f5f5 : 0xffffff), 0);
            lv_obj_set_style_bg_opa(row_bg, 255, 0);
            lv_obj_set_style_border_width(row_bg, 0, 0);
            lv_obj_set_style_radius(row_bg, 0, 0);
            lv_obj_set_style_pad_all(row_bg, 0, 0);

            char qty_str[16], price_str[16];
            snprintf(qty_str, sizeof(qty_str), "%d", json_int(item, "quantity", 0));
            snprintf(price_str, sizeof(price_str), "%.0f", json_double(item, "price", 0));

            const char *vals[] = {
                json_str(item, "code", ""),
                json_str(item, "name", ""),
                json_str(item, "brand", ""),
                json_str(item, "color", ""),
                json_str(item, "size", ""),
                qty_str,
                price_str
            };

            for (int j = 0; j < 7; j++) {
                lv_obj_t *cell = lv_label_create(row_bg);
                mark_dynamic(cell);
                lv_label_set_text(cell, vals[j]);
                lv_obj_set_pos(cell, col_x[j], 8);
                lv_obj_set_style_text_font(cell, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
                lv_obj_set_style_text_color(cell, lv_color_hex(0x333333), 0);
            }
        }
    }

    /* 页码信息 */
    lv_obj_t *page_lbl = lv_label_create(cont);
    mark_dynamic(page_lbl);
    char page_str[32];
    snprintf(page_str, sizeof(page_str), "%d / %d", s_stock_page, s_stock_total_pages);
    lv_label_set_text(page_lbl, page_str);
    lv_obj_set_pos(page_lbl, 400, 492);
    lv_obj_set_style_text_font(page_lbl, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_style_text_color(page_lbl, lv_color_hex(0x666666), 0);

    lvgl_unlock();
    cJSON_Delete(resp);
}

void ui_load_stock_records(const char *date, int page)
{
    if (!mqtt_is_connected()) return;
    if (strlen(date) == 0) get_today_str(s_stock_date, sizeof(s_stock_date));
    else strncpy(s_stock_date, date, sizeof(s_stock_date) - 1);
    s_stock_page = page;
    post_request(REQ_STOCK_RECORDS, s_stock_date, page);
}

/* ======================== 售货记录 ======================== */

static void sell_records_resp_cb(cJSON *resp)
{
    cJSON *data = cJSON_GetObjectItem(resp, "data");
    if (!data) { cJSON_Delete(resp); return; }

    s_sell_page = json_int(data, "page", 1);
    s_sell_total_pages = json_int(data, "total_pages", 1);

	lvgl_lock();

	if (s_active_screen != SCREEN_SELL) {
		lvgl_unlock();
		cJSON_Delete(resp);
		return;
	}

	lv_obj_t *cont = guider_ui.screen_sell_cont_1;
    if (!cont) { lvgl_unlock(); cJSON_Delete(resp); return; }

    clear_dynamic_children(cont);

    /* 表头 */
    const char *headers[] = {"编号", "款式", "牌子", "颜色", "尺码", "数量", "金额"};
    int col_x[] = {10, 130, 250, 370, 480, 580, 680};

    lv_obj_t *hdr_row = lv_obj_create(cont);
    mark_dynamic(hdr_row);
    lv_obj_set_size(hdr_row, 840, 35);
    lv_obj_set_pos(hdr_row, 10, 70);
    lv_obj_set_style_bg_color(hdr_row, lv_color_hex(0x2195f6), 0);
    lv_obj_set_style_bg_opa(hdr_row, 255, 0);
    lv_obj_set_style_border_width(hdr_row, 0, 0);
    lv_obj_set_style_radius(hdr_row, 0, 0);
    lv_obj_set_style_pad_all(hdr_row, 0, 0);

    for (int i = 0; i < 7; i++) {
        lv_obj_t *h = lv_label_create(hdr_row);
        mark_dynamic(h);
        lv_label_set_text(h, headers[i]);
        lv_obj_set_pos(h, col_x[i], 7);
        lv_obj_set_style_text_font(h, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
        lv_obj_set_style_text_color(h, lv_color_hex(0xffffff), 0);
    }

    /* 数据行 */
    cJSON *items = cJSON_GetObjectItem(data, "items");
    if (items && cJSON_IsArray(items)) {
        int row_count = cJSON_GetArraySize(items);
        for (int i = 0; i < row_count; i++) {
            cJSON *item = cJSON_GetArrayItem(items, i);
            int y = 110 + i * 42;

            lv_obj_t *row_bg = lv_obj_create(cont);
            mark_dynamic(row_bg);
            lv_obj_set_size(row_bg, 840, 38);
            lv_obj_set_pos(row_bg, 10, y);
            lv_obj_set_style_bg_color(row_bg, lv_color_hex(i % 2 ? 0xf5f5f5 : 0xffffff), 0);
            lv_obj_set_style_bg_opa(row_bg, 255, 0);
            lv_obj_set_style_border_width(row_bg, 0, 0);
            lv_obj_set_style_radius(row_bg, 0, 0);
            lv_obj_set_style_pad_all(row_bg, 0, 0);

            char qty_str[16], total_str[16];
            snprintf(qty_str, sizeof(qty_str), "%d", json_int(item, "quantity", 0));
            snprintf(total_str, sizeof(total_str), "%.0f", json_double(item, "total_price", 0));

            const char *vals[] = {
                json_str(item, "code", ""),
                json_str(item, "name", ""),
                json_str(item, "brand", ""),
                json_str(item, "color", ""),
                json_str(item, "size", ""),
                qty_str,
                total_str
            };

            for (int j = 0; j < 7; j++) {
                lv_obj_t *cell = lv_label_create(row_bg);
                mark_dynamic(cell);
                lv_label_set_text(cell, vals[j]);
                lv_obj_set_pos(cell, col_x[j], 8);
                lv_obj_set_style_text_font(cell, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
                lv_obj_set_style_text_color(cell, lv_color_hex(0x333333), 0);
            }
        }
    }

    /* 页码 */
    lv_obj_t *page_lbl = lv_label_create(cont);
    mark_dynamic(page_lbl);
    char page_str[32];
    snprintf(page_str, sizeof(page_str), "%d / %d", s_sell_page, s_sell_total_pages);
    lv_label_set_text(page_lbl, page_str);
    lv_obj_set_pos(page_lbl, 400, 492);
    lv_obj_set_style_text_font(page_lbl, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_style_text_color(page_lbl, lv_color_hex(0x666666), 0);

    lvgl_unlock();
    cJSON_Delete(resp);
}

void ui_load_sell_records(const char *date, int page)
{
    if (!mqtt_is_connected()) return;
    if (strlen(date) == 0) get_today_str(s_sell_date, sizeof(s_sell_date));
    else strncpy(s_sell_date, date, sizeof(s_sell_date) - 1);
    s_sell_page = page;
    post_request(REQ_SELL_RECORDS, s_sell_date, page);
}

/* ======================== 查看页面 ======================== */

static void check_items_resp_cb(cJSON *resp)
{
    cJSON *data = cJSON_GetObjectItem(resp, "data");
    if (!data) { cJSON_Delete(resp); return; }

    s_check_page = json_int(data, "page", 1);
    s_check_total_pages = json_int(data, "total_pages", 1);

	lvgl_lock();

	if (s_active_screen != SCREEN_CHECK) {
		lvgl_unlock();
		cJSON_Delete(resp);
		return;
	}

	lv_obj_t *cont = guider_ui.screen_check_cont_1;
    if (!cont) { lvgl_unlock(); cJSON_Delete(resp); return; }

    clear_dynamic_children(cont);

    /* 表头 */
    const char *headers[] = {"编号", "款式", "牌子", "颜色", "库存", "价格"};
    int col_x[] = {10, 140, 270, 400, 510, 610};

    lv_obj_t *hdr_row = lv_obj_create(cont);
    mark_dynamic(hdr_row);
    lv_obj_set_size(hdr_row, 730, 35);
    lv_obj_set_pos(hdr_row, 10, 90);
    lv_obj_set_style_bg_color(hdr_row, lv_color_hex(0x2195f6), 0);
    lv_obj_set_style_bg_opa(hdr_row, 255, 0);
    lv_obj_set_style_border_width(hdr_row, 0, 0);
    lv_obj_set_style_radius(hdr_row, 0, 0);
    lv_obj_set_style_pad_all(hdr_row, 0, 0);

    for (int i = 0; i < 6; i++) {
        lv_obj_t *h = lv_label_create(hdr_row);
        mark_dynamic(h);
        lv_label_set_text(h, headers[i]);
        lv_obj_set_pos(h, col_x[i], 7);
        lv_obj_set_style_text_font(h, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
        lv_obj_set_style_text_color(h, lv_color_hex(0xffffff), 0);
    }

    /* 数据行 */
    cJSON *items = cJSON_GetObjectItem(data, "items");
    if (items && cJSON_IsArray(items)) {
        int row_count = cJSON_GetArraySize(items);
        for (int i = 0; i < row_count; i++) {
            cJSON *item = cJSON_GetArrayItem(items, i);
            int y = 130 + i * 42;

            lv_obj_t *row_bg = lv_obj_create(cont);
            mark_dynamic(row_bg);
            lv_obj_set_size(row_bg, 730, 38);
            lv_obj_set_pos(row_bg, 10, y);
            lv_obj_set_style_bg_color(row_bg, lv_color_hex(i % 2 ? 0xf5f5f5 : 0xffffff), 0);
            lv_obj_set_style_bg_opa(row_bg, 255, 0);
            lv_obj_set_style_border_width(row_bg, 0, 0);
            lv_obj_set_style_radius(row_bg, 0, 0);
            lv_obj_set_style_pad_all(row_bg, 0, 0);

            char stock_str[16], price_str[16];
            snprintf(stock_str, sizeof(stock_str), "%d", json_int(item, "total_stock", 0));
            snprintf(price_str, sizeof(price_str), "%.0f", json_double(item, "price", 0));

            const char *vals[] = {
                json_str(item, "code", ""),
                json_str(item, "name", ""),
                json_str(item, "brand", ""),
                json_str(item, "color", ""),
                stock_str,
                price_str
            };

            for (int j = 0; j < 6; j++) {
                lv_obj_t *cell = lv_label_create(row_bg);
                mark_dynamic(cell);
                lv_label_set_text(cell, vals[j]);
                lv_obj_set_pos(cell, col_x[j], 8);
                lv_obj_set_style_text_font(cell, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
                lv_obj_set_style_text_color(cell, lv_color_hex(0x333333), 0);
            }
        }
    }

    /* 页码 */
    lv_obj_t *page_lbl = lv_label_create(cont);
    mark_dynamic(page_lbl);
    char page_str[32];
    snprintf(page_str, sizeof(page_str), "%d / %d", s_check_page, s_check_total_pages);
    lv_label_set_text(page_lbl, page_str);
    lv_obj_set_pos(page_lbl, 350, 492);
    lv_obj_set_style_text_font(page_lbl, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_style_text_color(page_lbl, lv_color_hex(0x666666), 0);

    lvgl_unlock();
    cJSON_Delete(resp);
}

void ui_load_check_category(const char *category, int page)
{
    if (!mqtt_is_connected()) return;
    if (category && category != s_check_category) strncpy(s_check_category, category, sizeof(s_check_category) - 1);
    s_check_page = page;
    post_request(REQ_CHECK_CATEGORY, s_check_category, page);
}

void ui_search_items(const char *keyword, int page)
{
    if (!mqtt_is_connected()) return;
    if (keyword) strncpy(s_check_keyword, keyword, sizeof(s_check_keyword) - 1);
    s_check_page = page;
    post_request(REQ_SEARCH_ITEMS, s_check_keyword, page);
}

/* ======================== 设置页面 ======================== */

/* ---- 亮度 NVS 持久化 ---- */
#define BRIGHTNESS_NVS_NS   "display"
#define BRIGHTNESS_NVS_KEY  "brightness"

static void brightness_save_nvs(int val)
{
    nvs_handle_t nvs;
    if (nvs_open(BRIGHTNESS_NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_i8(nvs, BRIGHTNESS_NVS_KEY, (int8_t)val);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static int brightness_load_nvs(void)
{
    nvs_handle_t nvs;
    int8_t val = 50;  /* 默认亮度 */
    if (nvs_open(BRIGHTNESS_NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_i8(nvs, BRIGHTNESS_NVS_KEY, &val);
        nvs_close(nvs);
    }
    return (int)val;
}

static void set_brightness_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    if (val < 20) val = 20;
    display_brightness_set(val);
    brightness_save_nvs(val);  /* 保存到 NVS */
}

/* WiFi 设置区域点击回调：打开 WiFi 配置面板 */
static void wifi_area_click_cb(lv_event_t *e)
{
    wifi_config_ui_open();
}

/* ======================== 前向声明（事件回调） ======================== */

static void stock_prev_page_cb(lv_event_t *e);
static void stock_next_page_cb(lv_event_t *e);
static void sell_prev_page_cb(lv_event_t *e);
static void sell_next_page_cb(lv_event_t *e);
static void check_prev_page_cb(lv_event_t *e);
static void check_next_page_cb(lv_event_t *e);
static void check_category_btn_cb(lv_event_t *e);
static void check_search_cb(lv_event_t *e);

/* 日历/新增/删除按钮回调（定义在下方，此处前向声明） */
static void stock_calendar_btn_cb(lv_event_t *e);
static void stock_add_btn_cb(lv_event_t *e);
static void stock_del_btn_cb(lv_event_t *e);
static void sell_calendar_btn_cb(lv_event_t *e);
static void sell_add_btn_cb(lv_event_t *e);
static void sell_del_btn_cb(lv_event_t *e);

/* ======================== 页面进入回调 ======================== */

/* 进货页面进入时注册事件 + 加载数据 */
void on_enter_screen_stock(void)
{
    s_active_screen = SCREEN_STOCK;
    if (s_overview_req_id > 0) { mqtt_cancel_callback(s_overview_req_id); s_overview_req_id = -1; }

    /* 分页按钮 */
    if (guider_ui.screen_stock_btn_3)
        lv_obj_add_event_cb(guider_ui.screen_stock_btn_3, stock_prev_page_cb, LV_EVENT_CLICKED, NULL);
    if (guider_ui.screen_stock_btn_4)
        lv_obj_add_event_cb(guider_ui.screen_stock_btn_4, stock_next_page_cb, LV_EVENT_CLICKED, NULL);

    /* 日历/新增/删除按钮 */
    if (guider_ui.screen_stock_btn_2)
        lv_obj_add_event_cb(guider_ui.screen_stock_btn_2, stock_calendar_btn_cb, LV_EVENT_CLICKED, NULL);
    if (guider_ui.screen_stock_btn_6)
        lv_obj_add_event_cb(guider_ui.screen_stock_btn_6, stock_add_btn_cb, LV_EVENT_CLICKED, NULL);
    if (guider_ui.screen_stock_btn_5)
        lv_obj_add_event_cb(guider_ui.screen_stock_btn_5, stock_del_btn_cb, LV_EVENT_CLICKED, NULL);

    get_today_str(s_stock_date, sizeof(s_stock_date));
    ui_load_stock_records(s_stock_date, 1);
}

/* 售货页面进入时注册事件 + 加载数据 */
void on_enter_screen_sell(void)
{
    s_active_screen = SCREEN_SELL;
    if (s_overview_req_id > 0) { mqtt_cancel_callback(s_overview_req_id); s_overview_req_id = -1; }

    /* 分页按钮 */
    if (guider_ui.screen_sell_btn_3)
        lv_obj_add_event_cb(guider_ui.screen_sell_btn_3, sell_prev_page_cb, LV_EVENT_CLICKED, NULL);
    if (guider_ui.screen_sell_btn_4)
        lv_obj_add_event_cb(guider_ui.screen_sell_btn_4, sell_next_page_cb, LV_EVENT_CLICKED, NULL);

    /* 日历/新增/删除按钮 */
    if (guider_ui.screen_sell_btn_7)
        lv_obj_add_event_cb(guider_ui.screen_sell_btn_7, sell_calendar_btn_cb, LV_EVENT_CLICKED, NULL);
    if (guider_ui.screen_sell_btn_5)
        lv_obj_add_event_cb(guider_ui.screen_sell_btn_5, sell_add_btn_cb, LV_EVENT_CLICKED, NULL);
    if (guider_ui.screen_sell_btn_6)
        lv_obj_add_event_cb(guider_ui.screen_sell_btn_6, sell_del_btn_cb, LV_EVENT_CLICKED, NULL);

    get_today_str(s_sell_date, sizeof(s_sell_date));
    ui_load_sell_records(s_sell_date, 1);
}

/* 查看页面进入时注册事件 + 加载数据 */
void on_enter_screen_check(void)
{
    s_active_screen = SCREEN_CHECK;
    if (s_overview_req_id > 0) { mqtt_cancel_callback(s_overview_req_id); s_overview_req_id = -1; }

    if (guider_ui.screen_check_btn_2)
        lv_obj_add_event_cb(guider_ui.screen_check_btn_2, check_category_btn_cb, LV_EVENT_CLICKED, (void *)"上衣");
    if (guider_ui.screen_check_btn_3)
        lv_obj_add_event_cb(guider_ui.screen_check_btn_3, check_category_btn_cb, LV_EVENT_CLICKED, (void *)"裤子");
    if (guider_ui.screen_check_btn_4)
        lv_obj_add_event_cb(guider_ui.screen_check_btn_4, check_category_btn_cb, LV_EVENT_CLICKED, (void *)"外套");
    if (guider_ui.screen_check_btn_5)
        lv_obj_add_event_cb(guider_ui.screen_check_btn_5, check_category_btn_cb, LV_EVENT_CLICKED, (void *)"配饰");
    if (guider_ui.screen_check_btn_6)
        lv_obj_add_event_cb(guider_ui.screen_check_btn_6, check_prev_page_cb, LV_EVENT_CLICKED, NULL);
    if (guider_ui.screen_check_btn_7)
        lv_obj_add_event_cb(guider_ui.screen_check_btn_7, check_next_page_cb, LV_EVENT_CLICKED, NULL);
    if (guider_ui.screen_check_img_1)
        lv_obj_add_event_cb(guider_ui.screen_check_img_1, check_search_cb, LV_EVENT_CLICKED, NULL);
    ui_load_check_category(s_check_category, 1);
}

/* 设置页面进入时注册事件 + 恢复持久化状态 */
void on_enter_screen_set(void)
{
    s_active_screen = SCREEN_SET;
    if (s_overview_req_id > 0) { mqtt_cancel_callback(s_overview_req_id); s_overview_req_id = -1; }

    /* 从 NVS 恢复亮度滑块位置 */
    int saved_brightness = brightness_load_nvs();
    if (guider_ui.screen_set_slider_1) {
        lv_slider_set_value(guider_ui.screen_set_slider_1, saved_brightness, LV_ANIM_OFF);
        lv_obj_add_event_cb(guider_ui.screen_set_slider_1, set_brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }
    display_brightness_set(saved_brightness);  /* 同步硬件亮度 */
    ESP_LOGI(TAG, "Settings: brightness restored to %d%%", saved_brightness);

    /* 从 NVS 恢复 WiFi 开关状态 */
    if (guider_ui.screen_set_sw_1) {
        char ssid[33] = {0};
        char pass[64] = {0};
        bool has_wifi = wifi_hosted_load_credentials(ssid, sizeof(ssid), pass, sizeof(pass));
        if (has_wifi && strlen(ssid) > 0) {
            lv_obj_add_state(guider_ui.screen_set_sw_1, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(guider_ui.screen_set_sw_1, LV_STATE_CHECKED);
        }
        ESP_LOGI(TAG, "Settings: WiFi switch %s (ssid=%s)",
                 (has_wifi && strlen(ssid) > 0) ? "ON" : "OFF", ssid);
    }

    /* WiFi 区域点击打开配置面板 */
    if (guider_ui.screen_set_img_2)
        lv_obj_add_event_cb(guider_ui.screen_set_img_2, wifi_area_click_cb, LV_EVENT_CLICKED, NULL);
    if (guider_ui.screen_set_label_2)
        lv_obj_add_event_cb(guider_ui.screen_set_label_2, wifi_area_click_cb, LV_EVENT_CLICKED, NULL);
    if (guider_ui.screen_set_sw_1)
        lv_obj_add_event_cb(guider_ui.screen_set_sw_1, wifi_area_click_cb, LV_EVENT_CLICKED, NULL);
}

/* ======================== 分页事件 ======================== */

static void stock_prev_page_cb(lv_event_t *e)
{
    if (s_stock_page > 1) ui_load_stock_records(s_stock_date, s_stock_page - 1);
}

static void stock_next_page_cb(lv_event_t *e)
{
    if (s_stock_page < s_stock_total_pages) ui_load_stock_records(s_stock_date, s_stock_page + 1);
}

static void sell_prev_page_cb(lv_event_t *e)
{
    if (s_sell_page > 1) ui_load_sell_records(s_sell_date, s_sell_page - 1);
}

static void sell_next_page_cb(lv_event_t *e)
{
    if (s_sell_page < s_sell_total_pages) ui_load_sell_records(s_sell_date, s_sell_page + 1);
}

static void check_prev_page_cb(lv_event_t *e)
{
    if (s_check_page > 1) ui_load_check_category(s_check_category, s_check_page - 1);
}

static void check_next_page_cb(lv_event_t *e)
{
    if (s_check_page < s_check_total_pages) ui_load_check_category(s_check_category, s_check_page + 1);
}

/* ======================== 分类按钮事件 ======================== */

static void check_category_btn_cb(lv_event_t *e)
{
    const char *cat = (const char *)lv_event_get_user_data(e);
    if (cat) {
        strncpy(s_check_category, cat, sizeof(s_check_category) - 1);
        ui_load_check_category(s_check_category, 1);
    }
}

/* ======================== 搜索事件 ======================== */

static void check_search_cb(lv_event_t *e)
{
    lv_obj_t *ta = guider_ui.screen_check_search_1;
    if (ta) {
        const char *text = lv_textarea_get_text(ta);
        if (text && strlen(text) > 0) {
            ui_search_items(text, 1);
        }
    }
}

/* ======================== 屏幕切换监控 ======================== */

/*
 * 问题：events_init.c 中 generated 回调用 LV_EVENT_ALL 注册，
 * 回调内调用 ui_load_scr_animation(is_clean=true) → lv_obj_clean(act_scr)
 * 会立即删除旧屏幕所有子对象（包括按钮），导致后续注册的 LV_EVENT_CLICKED 回调无法触发。
 *
 * 解决方案：不依赖按钮回调，改用 LVGL 定时器轮询检测屏幕切换，
 * 当检测到活动屏幕变化时，调用对应的 on_enter_screen_* 函数。
 */

static lv_obj_t *s_last_active_scr = NULL;

/* ======================== MQTT 请求异步任务 ======================== */
/*
 * 问题：所有 ui_load_* 函数最终调用 mqtt_send_request -> esp_mqtt_client_publish，
 * 这些函数可能在 lvgl_lock 内被调用（定时器回调、事件回调、custom_init）。
 * 若 esp_mqtt_client_publish 阻塞，且 MQTT 响应回调需要 lvgl_lock，则死锁。
 *
 * 方案：所有 MQTT 发送请求通过 FreeRTOS 队列交给独立任务执行，
 * 该任务不持有 LVGL 锁，可以安全调用 mqtt_send_request。
 */

/* 前向声明 */
static void overview_resp_cb(cJSON *resp);
static void stock_records_resp_cb(cJSON *resp);
static void sell_records_resp_cb(cJSON *resp);
static void check_items_resp_cb(cJSON *resp);

static void mqtt_req_task(void *arg)
{
    req_msg_t msg;
    for (;;) {
        if (xQueueReceive(s_req_queue, &msg, portMAX_DELAY) != pdTRUE) continue;

        switch (msg.type) {
        case REQ_OVERVIEW: {
            if (s_overview_req_id > 0) mqtt_cancel_callback(s_overview_req_id);
            s_overview_req_id = mqtt_send_request("get_overview", NULL, overview_resp_cb);
            break;
        }
        case REQ_STOCK_RECORDS: {
            cJSON *params = cJSON_CreateObject();
            cJSON_AddStringToObject(params, "date", msg.param1);
            cJSON_AddNumberToObject(params, "page", msg.param2);
            cJSON_AddNumberToObject(params, "page_size", 8);
            if (s_stock_req_id > 0) mqtt_cancel_callback(s_stock_req_id);
            s_stock_req_id = mqtt_send_request("get_stock_records", params, stock_records_resp_cb);
            cJSON_Delete(params);
            break;
        }
        case REQ_SELL_RECORDS: {
            cJSON *params = cJSON_CreateObject();
            cJSON_AddStringToObject(params, "date", msg.param1);
            cJSON_AddNumberToObject(params, "page", msg.param2);
            cJSON_AddNumberToObject(params, "page_size", 8);
            if (s_sell_req_id > 0) mqtt_cancel_callback(s_sell_req_id);
            s_sell_req_id = mqtt_send_request("get_sell_records", params, sell_records_resp_cb);
            cJSON_Delete(params);
            break;
        }
        case REQ_CHECK_CATEGORY: {
            cJSON *params = cJSON_CreateObject();
            cJSON_AddStringToObject(params, "category", msg.param1);
            cJSON_AddNumberToObject(params, "page", msg.param2);
            cJSON_AddNumberToObject(params, "page_size", 8);
            if (s_check_req_id > 0) mqtt_cancel_callback(s_check_req_id);
            s_check_req_id = mqtt_send_request("get_by_category", params, check_items_resp_cb);
            cJSON_Delete(params);
            break;
        }
        case REQ_SEARCH_ITEMS: {
            cJSON *params = cJSON_CreateObject();
            cJSON_AddStringToObject(params, "keyword", msg.param1);
            cJSON_AddNumberToObject(params, "page", msg.param2);
            cJSON_AddNumberToObject(params, "page_size", 8);
            if (s_check_req_id > 0) mqtt_cancel_callback(s_check_req_id);
            s_check_req_id = mqtt_send_request("search", params, check_items_resp_cb);
            cJSON_Delete(params);
            break;
        }
        case REQ_ADD_STOCK_RECORD: {
            cJSON *params = cJSON_CreateObject();
            cJSON_AddStringToObject(params, "date", msg.param1);
            cJSON_AddNumberToObject(params, "clothing_id", msg.param2);
            cJSON_AddStringToObject(params, "size", msg.param3);
            cJSON_AddNumberToObject(params, "quantity", atoi(msg.param4));
            mqtt_send_request("add_stock_record", params, stock_records_resp_cb);
            cJSON_Delete(params);
            /* 操作后刷新列表 */
            post_request(REQ_STOCK_RECORDS, msg.param1, s_stock_page);
            break;
        }
        case REQ_ADD_STOCK_FULL: {
            cJSON *params = cJSON_CreateObject();
            cJSON_AddStringToObject(params, "date", msg.param1);
            cJSON_AddStringToObject(params, "code", msg.param5);
            cJSON_AddStringToObject(params, "name", msg.param6);
            cJSON_AddStringToObject(params, "brand", msg.param7);
            cJSON_AddStringToObject(params, "color", msg.param8);
            cJSON_AddStringToObject(params, "size", msg.param3);
            cJSON_AddNumberToObject(params, "quantity", atoi(msg.param4));
            cJSON_AddNumberToObject(params, "price", atof(msg.param9));
            mqtt_send_request("add_stock_full", params, stock_records_resp_cb);
            cJSON_Delete(params);
            /* 操作后刷新列表 */
            post_request(REQ_STOCK_RECORDS, msg.param1, s_stock_page);
            break;
        }
        case REQ_DELETE_STOCK_RECORD: {
            cJSON *params = cJSON_CreateObject();
            cJSON_AddNumberToObject(params, "id", msg.param2);
            mqtt_send_request("delete_stock_record", params, stock_records_resp_cb);
            cJSON_Delete(params);
            post_request(REQ_STOCK_RECORDS, s_stock_date, s_stock_page);
            break;
        }
        case REQ_DELETE_STOCK_BY_CODE: {
            cJSON *params = cJSON_CreateObject();
            cJSON_AddStringToObject(params, "code", msg.param5);
            mqtt_send_request("delete_stock_by_code", params, stock_records_resp_cb);
            cJSON_Delete(params);
            post_request(REQ_STOCK_RECORDS, s_stock_date, s_stock_page);
            break;
        }
        case REQ_ADD_SELL_RECORD: {
            cJSON *params = cJSON_CreateObject();
            cJSON_AddStringToObject(params, "date", msg.param1);
            cJSON_AddNumberToObject(params, "clothing_id", msg.param2);
            cJSON_AddStringToObject(params, "size", msg.param3);
            cJSON_AddNumberToObject(params, "quantity", atoi(msg.param4));
            mqtt_send_request("add_sell_record", params, sell_records_resp_cb);
            cJSON_Delete(params);
            post_request(REQ_SELL_RECORDS, msg.param1, s_sell_page);
            break;
        }
        case REQ_ADD_SELL_FULL: {
            cJSON *params = cJSON_CreateObject();
            cJSON_AddStringToObject(params, "date", msg.param1);
            cJSON_AddStringToObject(params, "code", msg.param5);
            cJSON_AddStringToObject(params, "name", msg.param6);
            cJSON_AddStringToObject(params, "brand", msg.param7);
            cJSON_AddStringToObject(params, "color", msg.param8);
            cJSON_AddStringToObject(params, "size", msg.param3);
            cJSON_AddNumberToObject(params, "quantity", atoi(msg.param4));
            cJSON_AddNumberToObject(params, "total_price", atof(msg.param9));
            mqtt_send_request("add_sell_full", params, sell_records_resp_cb);
            cJSON_Delete(params);
            post_request(REQ_SELL_RECORDS, msg.param1, s_sell_page);
            break;
        }
        case REQ_DELETE_SELL_RECORD: {
            cJSON *params = cJSON_CreateObject();
            cJSON_AddNumberToObject(params, "id", msg.param2);
            mqtt_send_request("delete_sell_record", params, sell_records_resp_cb);
            cJSON_Delete(params);
            post_request(REQ_SELL_RECORDS, s_sell_date, s_sell_page);
            break;
        }
        case REQ_DELETE_SELL_BY_CODE: {
            cJSON *params = cJSON_CreateObject();
            cJSON_AddStringToObject(params, "code", msg.param5);
            mqtt_send_request("delete_sell_by_code", params, sell_records_resp_cb);
            cJSON_Delete(params);
            post_request(REQ_SELL_RECORDS, s_sell_date, s_sell_page);
            break;
        }
        case REQ_SYNC_SELL_RECORDS: {
            /* 每日同步：重新从后端拉取当天售货记录，确保前后端一致 */
            ESP_LOGI(TAG, "Daily sync: refreshing sell records for %s", s_sell_date);
            if (strlen(s_sell_date) > 0) {
                cJSON *params = cJSON_CreateObject();
                cJSON_AddStringToObject(params, "date", s_sell_date);
                cJSON_AddNumberToObject(params, "page", s_sell_page);
                cJSON_AddNumberToObject(params, "page_size", 8);
                if (s_sell_req_id > 0) mqtt_cancel_callback(s_sell_req_id);
                s_sell_req_id = mqtt_send_request("get_sell_records", params, sell_records_resp_cb);
                cJSON_Delete(params);
            }
            /* 同时刷新概览（销售额等） */
            if (s_overview_req_id > 0) mqtt_cancel_callback(s_overview_req_id);
            s_overview_req_id = mqtt_send_request("get_overview", NULL, overview_resp_cb);
            break;
        }
        }
    }
}

/* 向请求队列发送消息（可在 LVGL 锁内安全调用，不会阻塞） */
static bool post_request(req_type_t type, const char *param1, int param2)
{
    if (!s_req_queue) return false;
    req_msg_t msg = {0};
    msg.type = type;
    msg.param2 = param2;
    if (param1) snprintf(msg.param1, sizeof(msg.param1), "%s", param1);
    return xQueueSend(s_req_queue, &msg, 0) == pdTRUE;
}

/* 向请求队列发送记录操作消息（带额外参数） */
static bool post_record_request(req_type_t type, const char *param1, int param2,
                                 const char *param3, const char *param4)
{
    if (!s_req_queue) return false;
    req_msg_t msg = {0};
    msg.type = type;
    msg.param2 = param2;
    if (param1) snprintf(msg.param1, sizeof(msg.param1), "%s", param1);
    if (param3) snprintf(msg.param3, sizeof(msg.param3), "%s", param3);
    if (param4) snprintf(msg.param4, sizeof(msg.param4), "%s", param4);
    return xQueueSend(s_req_queue, &msg, 0) == pdTRUE;
}

/* 向请求队列发送完整入库消息（含服装详细信息） */
static bool post_stock_full_request(const char *date, const char *code, const char *name,
                                     const char *brand, const char *color,
                                     const char *size, const char *quantity, const char *price)
{
    if (!s_req_queue) return false;
    req_msg_t msg = {0};
    msg.type = REQ_ADD_STOCK_FULL;
    if (date)  snprintf(msg.param1, sizeof(msg.param1), "%s", date);
    if (size)  snprintf(msg.param3, sizeof(msg.param3), "%s", size);
    if (quantity) snprintf(msg.param4, sizeof(msg.param4), "%s", quantity);
    if (code)  snprintf(msg.param5, sizeof(msg.param5), "%s", code);
    if (name)  snprintf(msg.param6, sizeof(msg.param6), "%s", name);
    if (brand) snprintf(msg.param7, sizeof(msg.param7), "%s", brand);
    if (color) snprintf(msg.param8, sizeof(msg.param8), "%s", color);
    if (price) snprintf(msg.param9, sizeof(msg.param9), "%s", price);
    return xQueueSend(s_req_queue, &msg, 0) == pdTRUE;
}

/* 向请求队列发送完整售货消息（含服装详细信息+金额） */
static bool post_sell_full_request(const char *date, const char *code, const char *name,
                                    const char *brand, const char *color,
                                    const char *size, const char *quantity, const char *amount)
{
    if (!s_req_queue) return false;
    req_msg_t msg = {0};
    msg.type = REQ_ADD_SELL_FULL;
    if (date)  snprintf(msg.param1, sizeof(msg.param1), "%s", date);
    if (size)  snprintf(msg.param3, sizeof(msg.param3), "%s", size);
    if (quantity) snprintf(msg.param4, sizeof(msg.param4), "%s", quantity);
    if (code)  snprintf(msg.param5, sizeof(msg.param5), "%s", code);
    if (name)  snprintf(msg.param6, sizeof(msg.param6), "%s", name);
    if (brand) snprintf(msg.param7, sizeof(msg.param7), "%s", brand);
    if (color) snprintf(msg.param8, sizeof(msg.param8), "%s", color);
    if (amount) snprintf(msg.param9, sizeof(msg.param9), "%s", amount);
    return xQueueSend(s_req_queue, &msg, 0) == pdTRUE;
}

/* ======================== SNTP 时间更新定时器 ======================== */

/* 每秒更新主页时钟和日期（SNTP 同步后显示真实时间） */
static void sntp_clock_timer_cb(lv_timer_t *timer)
{
    /* 确保 SNTP 已初始化（网络恢复后补初始化） */
    if (s_wifi_connected && !s_sntp_inited) {
        ensure_sntp_init();
    }

    /* 更新主屏时钟 */
    if (guider_ui.screen_digital_clock_1 && lv_obj_is_valid(guider_ui.screen_digital_clock_1)) {
        char time_buf[16];
        get_time_str(time_buf, sizeof(time_buf));
        lv_label_set_text(guider_ui.screen_digital_clock_1, time_buf);
    }

    /* 更新主屏日期 */
    if (guider_ui.screen_datetext_1 && lv_obj_is_valid(guider_ui.screen_datetext_1)) {
        char date_buf[16];
        get_date_display_str(date_buf, sizeof(date_buf));
        lv_label_set_text(guider_ui.screen_datetext_1, date_buf);
    }

    /* 每日同步检测：日期跨过0点时触发一次同步 */
    if (s_sntp_synced && mqtt_is_connected()) {
        char today[16];
        get_today_str(today, sizeof(today));
        if (strlen(s_last_sync_date) > 0 && strcmp(today, s_last_sync_date) != 0) {
            ESP_LOGI(TAG, "Date changed: %s -> %s, triggering daily sync", s_last_sync_date, today);
            post_request(REQ_SYNC_SELL_RECORDS, today, 0);
            /* 同步后刷新入库页日期 */
            snprintf(s_stock_date, sizeof(s_stock_date), "%s", today);
            snprintf(s_sell_date, sizeof(s_sell_date), "%s", today);
        }
        snprintf(s_last_sync_date, sizeof(s_last_sync_date), "%s", today);
    }
}

/* ======================== 日历弹窗（入库/售货页选择日期） ======================== */

static lv_obj_t *s_stock_calendar = NULL;
static lv_obj_t *s_sell_calendar = NULL;

/* 入库页日历选择回调 */
static void stock_calendar_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_calendar_date_t date;
    if (lv_calendar_get_pressed_date(lv_event_get_current_target(e), &date) != LV_RESULT_OK) return;

    snprintf(s_stock_date, sizeof(s_stock_date), "%04d-%02d-%02d", date.year, date.month, date.day);
    ESP_LOGI(TAG, "Stock date selected: %s", s_stock_date);

    /* 关闭日历 */
    lv_obj_remove_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(lv_layer_top(), LV_OPA_TRANSP, 0);
    lv_obj_delete(s_stock_calendar);
    s_stock_calendar = NULL;

    /* 按新日期加载数据 */
    ui_load_stock_records(s_stock_date, 1);
}

/* 售货页日历选择回调 */
static void sell_calendar_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_calendar_date_t date;
    if (lv_calendar_get_pressed_date(lv_event_get_current_target(e), &date) != LV_RESULT_OK) return;

    snprintf(s_sell_date, sizeof(s_sell_date), "%04d-%02d-%02d", date.year, date.month, date.day);
    ESP_LOGI(TAG, "Sell date selected: %s", s_sell_date);

    lv_obj_remove_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(lv_layer_top(), LV_OPA_TRANSP, 0);
    lv_obj_delete(s_sell_calendar);
    s_sell_calendar = NULL;

    ui_load_sell_records(s_sell_date, 1);
}

/* 打开入库页日历弹窗 */
static void stock_open_calendar_cb(lv_event_t *e)
{
    if (s_stock_calendar) return;
    lv_obj_add_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(lv_layer_top(), LV_OPA_50, 0);
    lv_obj_set_style_bg_color(lv_layer_top(), lv_color_hex(0x000000), 0);

    s_stock_calendar = lv_calendar_create(lv_layer_top());
    lv_obj_set_size(s_stock_calendar, 500, 400);
    lv_obj_align(s_stock_calendar, LV_ALIGN_CENTER, 0, 0);
    lv_calendar_header_arrow_create(s_stock_calendar);

    /* 解析当前日期 */
    int y = 2026, m = 5, d = 20;
    sscanf(s_stock_date, "%d-%d-%d", &y, &m, &d);
    lv_calendar_set_showed_date(s_stock_calendar, y, m);
    lv_calendar_date_t today = { .year = y, .month = m, .day = d };
    lv_calendar_set_highlighted_dates(s_stock_calendar, &today, 1);

    lv_obj_add_event_cb(s_stock_calendar, stock_calendar_event_cb, LV_EVENT_ALL, NULL);
}

/* 打开售货页日历弹窗 */
static void sell_open_calendar_cb(lv_event_t *e)
{
    if (s_sell_calendar) return;
    lv_obj_add_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(lv_layer_top(), LV_OPA_50, 0);
    lv_obj_set_style_bg_color(lv_layer_top(), lv_color_hex(0x000000), 0);

    s_sell_calendar = lv_calendar_create(lv_layer_top());
    lv_obj_set_size(s_sell_calendar, 500, 400);
    lv_obj_align(s_sell_calendar, LV_ALIGN_CENTER, 0, 0);
    lv_calendar_header_arrow_create(s_sell_calendar);

    int y = 2026, m = 5, d = 20;
    sscanf(s_sell_date, "%d-%d-%d", &y, &m, &d);
    lv_calendar_set_showed_date(s_sell_calendar, y, m);
    lv_calendar_date_t today = { .year = y, .month = m, .day = d };
    lv_calendar_set_highlighted_dates(s_sell_calendar, &today, 1);

    lv_obj_add_event_cb(s_sell_calendar, sell_calendar_event_cb, LV_EVENT_ALL, NULL);
}

/* ======================== 新增记录对话框 ======================== */

static lv_obj_t *s_del_dialog = NULL;       /* 前向声明，供 ta_kb_avoid_cb 使用 */
static lv_obj_t *s_add_dialog = NULL;
static lv_obj_t *s_add_ta_code = NULL;   /* 款号(code) 文本输入 */
static lv_obj_t *s_add_dd_name = NULL;   /* 款式(name) 下拉框 */
static lv_obj_t *s_add_dd_brand = NULL;  /* 牌子(brand) 下拉框 */
static lv_obj_t *s_add_dd_color = NULL;  /* 颜色(color) 下拉框 */
static lv_obj_t *s_add_dd_size = NULL;   /* 尺码(size) 下拉框 */
static lv_obj_t *s_add_ta_qty = NULL;    /* 数量 文本输入 */
static lv_obj_t *s_add_ta_price = NULL;  /* 出售价格/金额 文本输入 */
static int s_add_dialog_type = 0;        /* 0=入库, 1=售货 */

/* 延迟绑定键盘：避免 lv_keyboard_set_textarea 在 FOCUSED 回调中
   触发旧 textarea 的 DEFOCUSED 事件，导致事件级联和键盘绑定错乱 */
static lv_obj_t *s_deferred_kb_ta = NULL;

static void kb_bind_deferred_cb(lv_timer_t *timer)
{
    if (guider_ui.g_kb_top_layer && s_deferred_kb_ta) {
        lv_keyboard_set_textarea(guider_ui.g_kb_top_layer, s_deferred_kb_ta);
        lv_obj_remove_flag(guider_ui.g_kb_top_layer, LV_OBJ_FLAG_HIDDEN);
    }
    s_deferred_kb_ta = NULL;
    lv_timer_delete(timer);
}

/* 键盘关闭（READY/CANCEL）时：恢复对话框位置 */
static void kb_dismiss_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_READY && code != LV_EVENT_CANCEL) return;

    /* 解绑键盘 */
    if (guider_ui.g_kb_top_layer) {
        lv_keyboard_set_textarea(guider_ui.g_kb_top_layer, NULL);
    }

    /* 恢复对话框位置 */
    if (s_add_dialog) {
        lv_obj_align(s_add_dialog, LV_ALIGN_TOP_MID, 0, 5);
    }
    if (s_del_dialog) {
        lv_obj_align(s_del_dialog, LV_ALIGN_TOP_MID, 0, 20);
    }
}

/* textarea 焦点变化时：延迟绑定键盘 + 上移对话框避免被键盘遮挡 */
static void ta_kb_avoid_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    lv_obj_t *dlg = (lv_obj_t *)lv_event_get_user_data(e);

    if (code == LV_EVENT_FOCUSED) {
        /* 延迟1ms绑定键盘，避免在事件回调内直接调用 lv_keyboard_set_textarea
           导致旧 textarea DEFOCUSED 事件级联、键盘绑定错乱 */
        s_deferred_kb_ta = ta;
        lv_timer_create(kb_bind_deferred_cb, 1, NULL);

        if (dlg) {
            lv_display_t *disp = lv_display_get_default();
            lv_coord_t hor = lv_display_get_horizontal_resolution(disp);
            lv_coord_t ver = lv_display_get_vertical_resolution(disp);
            if (hor > ver) {
                /* 横屏：键盘在左侧，对话框无需上移 */
                if (dlg == s_add_dialog) {
                    lv_obj_align(dlg, LV_ALIGN_TOP_MID, 0, 5);
                } else if (dlg == s_del_dialog) {
                    lv_obj_align(dlg, LV_ALIGN_TOP_MID, 0, 20);
                }
            } else {
                lv_obj_set_y(dlg, -130);  /* 竖屏：大幅上移避开键盘 */
            }
        }
    }
    /* 不在 DEFOCUSED 中恢复对话框位置 — 切换输入框时不应回移，
       位置恢复仅在键盘关闭（kb_dismiss_cb）或对话框关闭时执行 */
}

/* dropdown 点击时收起键盘 */
static void dd_click_cb(lv_event_t *e)
{
    if (guider_ui.g_kb_top_layer) {
        lv_keyboard_set_textarea(guider_ui.g_kb_top_layer, NULL);
        lv_obj_add_flag(guider_ui.g_kb_top_layer, LV_OBJ_FLAG_HIDDEN);
    }
}

static void add_dialog_close(void)
{
    /* 收起键盘 */
    if (guider_ui.g_kb_top_layer) {
        lv_keyboard_set_textarea(guider_ui.g_kb_top_layer, NULL);
        lv_obj_add_flag(guider_ui.g_kb_top_layer, LV_OBJ_FLAG_HIDDEN);
    }
    s_add_ta_code = NULL;
    s_add_dd_name = NULL;
    s_add_dd_brand = NULL;
    s_add_dd_color = NULL;
    s_add_dd_size = NULL;
    s_add_ta_qty = NULL;
    s_add_ta_price = NULL;

    if (s_add_dialog) {
        lv_obj_delete(s_add_dialog);
        s_add_dialog = NULL;
    }
    lv_obj_remove_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(lv_layer_top(), LV_OPA_TRANSP, 0);
}

static void add_dialog_confirm_cb(lv_event_t *e)
{
    if (!s_add_ta_code || !s_add_dd_name || !s_add_dd_brand ||
        !s_add_dd_color || !s_add_dd_size || !s_add_ta_qty || !s_add_ta_price) {
        add_dialog_close();
        return;
    }

    const char *code = lv_textarea_get_text(s_add_ta_code);
    const char *qty = lv_textarea_get_text(s_add_ta_qty);
    const char *price_str = lv_textarea_get_text(s_add_ta_price);

    if (!code || strlen(code) == 0 || !qty || strlen(qty) == 0) {
        ESP_LOGW(TAG, "Add record: missing required fields");
        add_dialog_close();
        return;
    }

    /* 获取下拉框选中值 */
    char name[32], brand[32], color[32], size[32];
    lv_dropdown_get_selected_str(s_add_dd_name, name, sizeof(name));
    lv_dropdown_get_selected_str(s_add_dd_brand, brand, sizeof(brand));
    lv_dropdown_get_selected_str(s_add_dd_color, color, sizeof(color));
    lv_dropdown_get_selected_str(s_add_dd_size, size, sizeof(size));

    if (s_add_dialog_type == 0) {
        /* 入库：发送完整服装信息+出售价格，后端自动查找/新建并更新价格 */
        post_stock_full_request(s_stock_date, code, name, brand, color, size, qty, price_str);
    } else {
        /* 售货：发送完整服装信息+金额 */
        post_sell_full_request(s_sell_date, code, name, brand, color, size, qty, price_str);
    }

    add_dialog_close();
}

static void add_dialog_cancel_cb(lv_event_t *e)
{
    add_dialog_close();
}

/* 打开新增记录对话框 */
static void open_add_dialog(int type)  /* 0=入库, 1=售货 */
{
    if (s_add_dialog) return;
    s_add_dialog_type = type;

    lv_obj_add_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(lv_layer_top(), LV_OPA_50, 0);
    lv_obj_set_style_bg_color(lv_layer_top(), lv_color_hex(0x000000), 0);

    s_add_dialog = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_add_dialog, 500, 290);
    lv_obj_align(s_add_dialog, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_bg_color(s_add_dialog, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_radius(s_add_dialog, 15, 0);
    lv_obj_set_style_pad_all(s_add_dialog, 10, 0);

    /* 标题 */
    lv_obj_t *title = lv_label_create(s_add_dialog);
    lv_label_set_text(title, type == 0 ? "新增入库记录" : "新增售货记录");
    lv_obj_set_style_text_font(title, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    /* ---------- 左列 ---------- */
    /* === 款号(code) 文本输入 === */
    lv_obj_t *lbl1 = lv_label_create(s_add_dialog);
    lv_label_set_text(lbl1, "款号");
    lv_obj_set_style_text_font(lbl1, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_pos(lbl1, 10, 32);
    s_add_ta_code = lv_textarea_create(s_add_dialog);
    lv_obj_set_size(s_add_ta_code, 120, 34);
    lv_obj_set_pos(s_add_ta_code, 60, 28);
    lv_textarea_set_one_line(s_add_ta_code, true);
    lv_obj_set_style_pad_all(s_add_ta_code, 0, 0);
    lv_obj_set_style_text_font(s_add_ta_code, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_add_event_cb(s_add_ta_code, ta_kb_avoid_cb, LV_EVENT_FOCUSED, s_add_dialog);

    /* === 款式(name) 下拉框 === */
    lv_obj_t *lbl2 = lv_label_create(s_add_dialog);
    lv_label_set_text(lbl2, "款式");
    lv_obj_set_style_text_font(lbl2, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_pos(lbl2, 10, 72);
    s_add_dd_name = lv_dropdown_create(s_add_dialog);
    lv_obj_set_size(s_add_dd_name, 120, 32);
    lv_obj_set_pos(s_add_dd_name, 60, 68);
    lv_dropdown_set_options(s_add_dd_name, "翻领T恤\n圆领T恤\n衬衫\n牛仔裤\n西裤\n休闲裤\n中裤\n羽绒服\n西装\n羊毛大衣\n马甲\n单西\n套西\n袜子\n内裤\n腰带\n领带\n领夹");
    lv_dropdown_set_symbol(s_add_dd_name, NULL);
    lv_obj_set_style_max_height(lv_dropdown_get_list(s_add_dd_name), 200, 0);
    lv_obj_set_style_text_font(s_add_dd_name, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_style_text_font(lv_dropdown_get_list(s_add_dd_name), &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_add_event_cb(s_add_dd_name, dd_click_cb, LV_EVENT_CLICKED, NULL);

    /* === 牌子(brand) 下拉框 === */
    lv_obj_t *lbl3 = lv_label_create(s_add_dialog);
    lv_label_set_text(lbl3, "牌子");
    lv_obj_set_style_text_font(lbl3, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_pos(lbl3, 10, 116);
    s_add_dd_brand = lv_dropdown_create(s_add_dialog);
    lv_obj_set_size(s_add_dd_brand, 120, 32);
    lv_obj_set_pos(s_add_dd_brand, 60, 112);
    lv_dropdown_set_options(s_add_dd_brand, "金盾\n花花公子");
    lv_dropdown_set_symbol(s_add_dd_brand, NULL);
    lv_obj_set_style_max_height(lv_dropdown_get_list(s_add_dd_brand), 80, 0);
    lv_obj_set_style_text_font(s_add_dd_brand, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_style_text_font(lv_dropdown_get_list(s_add_dd_brand), &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_add_event_cb(s_add_dd_brand, dd_click_cb, LV_EVENT_CLICKED, NULL);

    /* ---------- 右列 ---------- */
    /* === 颜色(color) 下拉框 === */
    lv_obj_t *lbl4 = lv_label_create(s_add_dialog);
    lv_label_set_text(lbl4, "颜色");
    lv_obj_set_style_text_font(lbl4, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_pos(lbl4, 270, 32);
    s_add_dd_color = lv_dropdown_create(s_add_dialog);
    lv_obj_set_size(s_add_dd_color, 130, 32);
    lv_obj_set_pos(s_add_dd_color, 320, 28);
    lv_dropdown_set_options(s_add_dd_color, "红\n橙\n黄\n绿\n青\n蓝\n紫");
    lv_dropdown_set_symbol(s_add_dd_color, NULL);
    lv_obj_set_style_max_height(lv_dropdown_get_list(s_add_dd_color), 100, 0);
    lv_obj_set_style_text_font(s_add_dd_color, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_style_text_font(lv_dropdown_get_list(s_add_dd_color), &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_add_event_cb(s_add_dd_color, dd_click_cb, LV_EVENT_CLICKED, NULL);

    /* === 尺码(size) 下拉框 === */
    lv_obj_t *lbl5 = lv_label_create(s_add_dialog);
    lv_label_set_text(lbl5, "尺码");
    lv_obj_set_style_text_font(lbl5, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_pos(lbl5, 270, 76);
    s_add_dd_size = lv_dropdown_create(s_add_dialog);
    lv_obj_set_size(s_add_dd_size, 130, 32);
    lv_obj_set_pos(s_add_dd_size, 320, 72);
    lv_dropdown_set_options(s_add_dd_size, "S\nM\nL\nXL\nXXL\n3XL");
    lv_dropdown_set_symbol(s_add_dd_size, NULL);
    lv_obj_set_style_max_height(lv_dropdown_get_list(s_add_dd_size), 100, 0);
    lv_obj_set_style_text_font(s_add_dd_size, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_style_text_font(lv_dropdown_get_list(s_add_dd_size), &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_add_event_cb(s_add_dd_size, dd_click_cb, LV_EVENT_CLICKED, NULL);

    /* === 数量 文本输入 === */
    lv_obj_t *lbl6 = lv_label_create(s_add_dialog);
    lv_label_set_text(lbl6, "数量");
    lv_obj_set_style_text_font(lbl6, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_pos(lbl6, 270, 120);
    s_add_ta_qty = lv_textarea_create(s_add_dialog);
    lv_obj_set_size(s_add_ta_qty, 130, 34);
    lv_obj_set_pos(s_add_ta_qty, 320, 116);
    lv_textarea_set_one_line(s_add_ta_qty, true);
    lv_obj_set_style_pad_all(s_add_ta_qty, 0, 0);
    lv_obj_set_style_text_font(s_add_ta_qty, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_add_event_cb(s_add_ta_qty, ta_kb_avoid_cb, LV_EVENT_FOCUSED, s_add_dialog);

    /* === 出售价格/金额 文本输入 === */
    lv_obj_t *lbl7 = lv_label_create(s_add_dialog);
    lv_label_set_text(lbl7, type == 0 ? "价格" : "金额");
    lv_obj_set_style_text_font(lbl7, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_pos(lbl7, 270, 164);
    s_add_ta_price = lv_textarea_create(s_add_dialog);
    lv_obj_set_size(s_add_ta_price, 130, 34);
    lv_obj_set_pos(s_add_ta_price, 320, 160);
    lv_textarea_set_one_line(s_add_ta_price, true);
    lv_obj_set_style_pad_all(s_add_ta_price, 0, 0);
    lv_obj_set_style_text_font(s_add_ta_price, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_add_event_cb(s_add_ta_price, ta_kb_avoid_cb, LV_EVENT_FOCUSED, s_add_dialog);

    /* 确认/取消按钮 */
    lv_obj_t *btn_ok = lv_button_create(s_add_dialog);
    lv_obj_set_size(btn_ok, 100, 36);
    lv_obj_set_pos(btn_ok, 110, 210);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0x2195f6), 0);
    lv_obj_t *lbl_ok = lv_label_create(btn_ok);
    lv_label_set_text(lbl_ok, "确认");
    lv_obj_set_style_text_font(lbl_ok, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_style_text_color(lbl_ok, lv_color_hex(0xffffff), 0);
    lv_obj_align(lbl_ok, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn_ok, add_dialog_confirm_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_cancel = lv_button_create(s_add_dialog);
    lv_obj_set_size(btn_cancel, 100, 36);
    lv_obj_set_pos(btn_cancel, 250, 210);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x999999), 0);
    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "取消");
    lv_obj_set_style_text_font(lbl_cancel, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_style_text_color(lbl_cancel, lv_color_hex(0xffffff), 0);
    lv_obj_align(lbl_cancel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn_cancel, add_dialog_cancel_cb, LV_EVENT_CLICKED, NULL);

    /* 延迟绑定键盘到款号输入框（避免事件级联） */
    s_deferred_kb_ta = s_add_ta_code;
    lv_timer_create(kb_bind_deferred_cb, 1, NULL);
    /* 横屏时键盘在左侧不遮挡对话框，无需大幅上移；竖屏才需要 */
    lv_display_t *disp = lv_display_get_default();
    lv_coord_t hor = lv_display_get_horizontal_resolution(disp);
    lv_coord_t ver = lv_display_get_vertical_resolution(disp);
    if (hor > ver) {
        lv_obj_align(s_add_dialog, LV_ALIGN_TOP_MID, 0, 5);   /* 横屏：保持原位 */
    } else {
        lv_obj_set_y(s_add_dialog, -130);                     /* 竖屏：大幅上移 */
    }
}

/* ======================== 删除记录对话框 ======================== */

static lv_obj_t *s_del_ta_id = NULL;
static int s_del_dialog_type = 0;  /* 0=入库, 1=售货 */

static void del_dialog_close(void)
{
    /* 收起键盘 */
    if (guider_ui.g_kb_top_layer) {
        lv_keyboard_set_textarea(guider_ui.g_kb_top_layer, NULL);
        lv_obj_add_flag(guider_ui.g_kb_top_layer, LV_OBJ_FLAG_HIDDEN);
    }
    s_del_ta_id = NULL;

    if (s_del_dialog) {
        lv_obj_delete(s_del_dialog);
        s_del_dialog = NULL;
    }
    lv_obj_remove_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(lv_layer_top(), LV_OPA_TRANSP, 0);
}

static void del_dialog_confirm_cb(lv_event_t *e)
{
    if (!s_del_ta_id) { del_dialog_close(); return; }
    const char *code = lv_textarea_get_text(s_del_ta_id);
    if (!code || strlen(code) == 0) { del_dialog_close(); return; }

    req_msg_t msg = {0};
    if (s_del_dialog_type == 0) {
        msg.type = REQ_DELETE_STOCK_BY_CODE;
    } else {
        msg.type = REQ_DELETE_SELL_BY_CODE;
    }
    snprintf(msg.param5, sizeof(msg.param5), "%s", code);
    xQueueSend(s_req_queue, &msg, 0);

    del_dialog_close();
}

static void del_dialog_cancel_cb(lv_event_t *e)
{
    del_dialog_close();
}

static void open_del_dialog(int type)  /* 0=入库, 1=售货 */
{
    if (s_del_dialog) return;
    s_del_dialog_type = type;

    lv_obj_add_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(lv_layer_top(), LV_OPA_50, 0);
    lv_obj_set_style_bg_color(lv_layer_top(), lv_color_hex(0x000000), 0);

    s_del_dialog = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_del_dialog, 380, 210);
    lv_obj_align(s_del_dialog, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_bg_color(s_del_dialog, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_radius(s_del_dialog, 15, 0);
    lv_obj_set_style_pad_all(s_del_dialog, 15, 0);

    lv_obj_t *title = lv_label_create(s_del_dialog);
    lv_label_set_text(title, type == 0 ? "删除入库记录" : "删除售货记录");
    lv_obj_set_style_text_font(title, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *lbl = lv_label_create(s_del_dialog);
    lv_label_set_text(lbl, "款号:");
    lv_obj_set_style_text_font(lbl, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_pos(lbl, 10, 50);
    s_del_ta_id = lv_textarea_create(s_del_dialog);
    lv_obj_set_size(s_del_ta_id, 240, 35);
    lv_obj_set_pos(s_del_ta_id, 70, 45);
    lv_textarea_set_placeholder_text(s_del_ta_id, "输入要删除的款号");
    lv_textarea_set_one_line(s_del_ta_id, true);
    lv_obj_set_style_text_font(s_del_ta_id, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_add_event_cb(s_del_ta_id, ta_kb_avoid_cb, LV_EVENT_FOCUSED, s_del_dialog);

    lv_obj_t *btn_ok = lv_button_create(s_del_dialog);
    lv_obj_set_size(btn_ok, 110, 40);
    lv_obj_set_pos(btn_ok, 50, 110);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0xe53935), 0);
    lv_obj_t *lbl_ok = lv_label_create(btn_ok);
    lv_label_set_text(lbl_ok, "确认删除");
    lv_obj_set_style_text_font(lbl_ok, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_style_text_color(lbl_ok, lv_color_hex(0xffffff), 0);
    lv_obj_align(lbl_ok, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn_ok, del_dialog_confirm_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_cancel = lv_button_create(s_del_dialog);
    lv_obj_set_size(btn_cancel, 120, 45);
    lv_obj_set_pos(btn_cancel, 220, 140);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x999999), 0);
    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "取消");
    lv_obj_set_style_text_font(lbl_cancel, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_style_text_color(lbl_cancel, lv_color_hex(0xffffff), 0);
    lv_obj_align(lbl_cancel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn_cancel, del_dialog_cancel_cb, LV_EVENT_CLICKED, NULL);

    /* 延迟绑定键盘到ID输入框（避免事件级联） */
    s_deferred_kb_ta = s_del_ta_id;
    lv_timer_create(kb_bind_deferred_cb, 1, NULL);
    lv_display_t *disp = lv_display_get_default();
    lv_coord_t hor = lv_display_get_horizontal_resolution(disp);
    lv_coord_t ver = lv_display_get_vertical_resolution(disp);
    if (hor > ver) {
        lv_obj_align(s_del_dialog, LV_ALIGN_TOP_MID, 0, 20); /* 横屏：保持原位 */
    } else {
        lv_obj_set_y(s_del_dialog, -130);                     /* 竖屏：大幅上移 */
    }
}

/* 入库页按钮回调：日历/新增/删除 */
static void stock_calendar_btn_cb(lv_event_t *e) { lvgl_lock(); stock_open_calendar_cb(e); lvgl_unlock(); }
static void stock_add_btn_cb(lv_event_t *e) { lvgl_lock(); open_add_dialog(0); lvgl_unlock(); }
static void stock_del_btn_cb(lv_event_t *e) { lvgl_lock(); open_del_dialog(0); lvgl_unlock(); }

/* 售货页按钮回调：日历/新增/删除 */
static void sell_calendar_btn_cb(lv_event_t *e) { lvgl_lock(); sell_open_calendar_cb(e); lvgl_unlock(); }
static void sell_add_btn_cb(lv_event_t *e) { lvgl_lock(); open_add_dialog(1); lvgl_unlock(); }
static void sell_del_btn_cb(lv_event_t *e) { lvgl_lock(); open_del_dialog(1); lvgl_unlock(); }

static void screen_monitor_cb(lv_timer_t *timer)
{
    lv_obj_t *act = lv_screen_active();
    if (act == s_last_active_scr) return;

    ESP_LOGI(TAG, "Screen changed -> %p (prev: %p)", act, s_last_active_scr);
    s_last_active_scr = act;

    if (act == guider_ui.screen) {
        cancel_child_requests();
        s_active_screen = SCREEN_MAIN;
        update_status_icons();
        /* 设置加载中提示文本 */
        if (guider_ui.screen_label_6)
            lv_label_set_text(guider_ui.screen_label_6, "店内库存总量: 加载中...");
        if (guider_ui.screen_label_7)
            lv_label_set_text(guider_ui.screen_label_7, "今日销售额: 加载中...");
        ui_load_overview();
    } else if (act == guider_ui.screen_stock) {
        on_enter_screen_stock();
    } else if (act == guider_ui.screen_sell) {
        on_enter_screen_sell();
    } else if (act == guider_ui.screen_check) {
        on_enter_screen_check();
    } else if (act == guider_ui.screen_set) {
        on_enter_screen_set();
    }
}

/* ======================== 初始化入口 ======================== */

void custom_init(lv_ui *ui)
{
    ESP_LOGI(TAG, "custom_init: registering business logic");

    /* 键盘使用内置字体（自定义字体缺特殊符号会显示方块） */
    if (ui->g_kb_top_layer) {
        lv_obj_set_style_text_font(ui->g_kb_top_layer, &lv_font_montserratMedium_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    /* 启动 MQTT 请求异步任务（在 LVGL 锁外执行 mqtt_send_request，避免死锁） */
    s_req_queue = xQueueCreate(8, sizeof(req_msg_t));
    xTaskCreate(mqtt_req_task, "mqtt_req", 4096, NULL, 5, NULL);

    /* 注册键盘关闭回调：键盘 READY/CANCEL 时恢复对话框位置 */
    if (ui->g_kb_top_layer) {
        lv_obj_add_event_cb(ui->g_kb_top_layer, kb_dismiss_cb, LV_EVENT_ALL, NULL);
    }

    /* --- 初始状态 --- */
    s_active_screen = SCREEN_MAIN;
    s_last_active_scr = ui->screen;
    update_status_icons();

    /* 初始化同步日期为今日（用于每日0点同步检测） */
    get_today_str(s_last_sync_date, sizeof(s_last_sync_date));
    ESP_LOGI(TAG, "Initial sync date: %s", s_last_sync_date);

    if (ui->screen_label_6)
        lv_label_set_text(ui->screen_label_6, "店内库存总量: 加载中...");
    if (ui->screen_label_7)
        lv_label_set_text(ui->screen_label_7, "今日销售额: 加载中...");

    /* 初始化主页时钟和日期为 SNTP 时间 */
    if (ui->screen_digital_clock_1) {
        char time_buf[16];
        get_time_str(time_buf, sizeof(time_buf));
        lv_label_set_text(ui->screen_digital_clock_1, time_buf);
    }
    if (ui->screen_datetext_1) {
        char date_buf[16];
        get_date_display_str(date_buf, sizeof(date_buf));
        lv_label_set_text(ui->screen_datetext_1, date_buf);
    }

    /* SNTP 时钟更新定时器（每秒刷新主页时钟和日期） */
    lv_timer_create(sntp_clock_timer_cb, 1000, NULL);

    /* 预创建所有屏幕（切屏时复用，不重复创建/删除，避免回调访问悬空指针） */
    setup_scr_screen_stock(&guider_ui);
    setup_scr_screen_sell(&guider_ui);
    setup_scr_screen_check(&guider_ui);
    setup_scr_screen_set(&guider_ui);
    ui->screen_del        = false;
    ui->screen_stock_del  = false;
    ui->screen_sell_del   = false;
    ui->screen_check_del  = false;
    ui->screen_set_del    = false;

    /* 启动屏幕切换监控定时器（50ms 轮询） */
    lv_timer_create(screen_monitor_cb, 50, NULL);

    /* 首次加载概览数据（通过队列异步发送，不会阻塞 LVGL） */
    post_request(REQ_OVERVIEW, NULL, 0);
}

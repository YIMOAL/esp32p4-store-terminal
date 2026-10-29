/*
 * wifi_config_ui.c — WiFi 配网界面
 * 实现：扫描列表、密码输入、连接/断开、记住密码
 */
#include "wifi_config_ui.h"
#include "wifi_hosted.h"
#include "gui_guider.h"
#include "adapter/lvgl_adapter.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "wifi_cfg_ui";

/* ======================== 样式常量 ======================== */
#define PANEL_W     820
#define PANEL_H     500
#define ITEM_H      54
#define ITEM_GAP    4
#define COLOR_PRIMARY   lv_color_hex(0x2195f6)
#define COLOR_BG        lv_color_hex(0xffffff)
#define COLOR_TEXT      lv_color_hex(0x333333)
#define COLOR_TEXT_LIGHT lv_color_hex(0x999999)
#define COLOR_CONNECTED lv_color_hex(0x4caf50)
#define COLOR_DANGER    lv_color_hex(0xe53935)

/* ======================== 字体 ======================== */
LV_FONT_DECLARE(lv_font_ZiTiQuanWeiJunHeiW22_25)
LV_FONT_DECLARE(lv_font_ZiTiQuanWeiJunHeiW22_16)
LV_FONT_DECLARE(lv_font_montserratMedium_16)

/* ======================== 内部状态 ======================== */
static lv_obj_t *s_panel        = NULL;   /* 主面板 */
static lv_obj_t *s_list_cont    = NULL;   /* WiFi 列表容器 */
static lv_obj_t *s_status_label = NULL;   /* 状态标签 */
static lv_obj_t *s_scan_btn     = NULL;   /* 扫描按钮 */
static lv_obj_t *s_disconnect_btn = NULL; /* 断开按钮 */
static lv_obj_t *s_loading_label = NULL;  /* 加载提示 */

/* 密码对话框 */
static lv_obj_t *s_pwd_dialog   = NULL;
static lv_obj_t *s_pwd_ta       = NULL;
static char s_target_ssid[33]   = "";     /* 待连接的 SSID */
static wifi_auth_mode_t s_target_auth = WIFI_AUTH_OPEN;

/* 延迟键盘绑定 */
static lv_obj_t *s_deferred_kb_ta = NULL;

/* 扫描结果缓存（用于密码对话框判断加密类型） */
#define MAX_SCAN_CACHE 32
static wifi_ap_info_t s_scan_cache[MAX_SCAN_CACHE];
static int s_scan_cache_count = 0;

/* ======================== 前向声明 ======================== */
static void wifi_scan_callback(wifi_ap_info_t *aps, int count);
static void start_scan(void);
static void open_password_dialog(const char *ssid, wifi_auth_mode_t auth_mode);
static void close_password_dialog(void);
static void do_connect(const char *ssid, const char *password);
static void do_disconnect(void);

/* ======================== 工具函数 ======================== */

/* 在扫描缓存中查找 SSID 的认证模式 */
static wifi_auth_mode_t find_auth_mode(const char *ssid)
{
    for (int i = 0; i < s_scan_cache_count; i++) {
        if (strcmp(s_scan_cache[i].ssid, ssid) == 0) {
            return s_scan_cache[i].auth_mode;
        }
    }
    return WIFI_AUTH_WPA2_PSK; /* 默认假设加密 */
}

/* 释放列表容器中所有动态子对象（含 ssid_copy 内存） */
static void clear_wifi_list(void)
{
    if (!s_list_cont) return;
    uint32_t cnt = lv_obj_get_child_count(s_list_cont);
    for (int32_t i = cnt - 1; i >= 0; i--) {
        lv_obj_t *child = lv_obj_get_child(s_list_cont, i);
        /* user_data 存放 ssid_copy 指针（item 级别）或 (void*)1（子标签） */
        void *ud = lv_obj_get_user_data(child);
        if (ud && ud != (void*)1) {
            lv_free(ud);  /* 释放 ssid_copy */
        } else {
            /* 可能是子标签，检查其子对象中的 item 的 user_data */
            /* 不需要，因为 ssid_copy 只挂在 item 上，子标签用 (void*)1 标记 */
        }
        lv_obj_delete(child);
    }
}

/* ======================== 键盘管理 ======================== */

static void kb_bind_deferred_cb(lv_timer_t *timer)
{
    if (guider_ui.g_kb_top_layer && s_deferred_kb_ta) {
        /* 将键盘移到所在层最前，避免被密码对话框遮挡 */
        lv_obj_move_foreground(guider_ui.g_kb_top_layer);
        lv_keyboard_set_textarea(guider_ui.g_kb_top_layer, s_deferred_kb_ta);
        lv_obj_remove_flag(guider_ui.g_kb_top_layer, LV_OBJ_FLAG_HIDDEN);
    }
    s_deferred_kb_ta = NULL;
    lv_timer_delete(timer);
}

static void kb_dismiss_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_READY && code != LV_EVENT_CANCEL) return;

    if (guider_ui.g_kb_top_layer) {
        lv_keyboard_set_textarea(guider_ui.g_kb_top_layer, NULL);
    }
    if (s_pwd_dialog) {
        lv_obj_align(s_pwd_dialog, LV_ALIGN_CENTER, 0, -50);
    }
}

static void pwd_ta_focus_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_FOCUSED) return;

    s_deferred_kb_ta = lv_event_get_target(e);
    lv_timer_create(kb_bind_deferred_cb, 1, NULL);

    if (s_pwd_dialog) {
        lv_display_t *disp = lv_display_get_default();
        lv_coord_t hor = lv_display_get_horizontal_resolution(disp);
        lv_coord_t ver = lv_display_get_vertical_resolution(disp);
        if (hor > ver) {
            /* 横屏：密码框大幅上移，避让底部键盘（键盘约高 280px） */
            lv_obj_align(s_pwd_dialog, LV_ALIGN_CENTER, 0, -180);
        } else {
            lv_obj_align(s_pwd_dialog, LV_ALIGN_CENTER, 0, -150);
        }
    }
}

/* ======================== 密码对话框 ======================== */

static void pwd_connect_cb(lv_event_t *e)
{
    if (!s_pwd_ta) { close_password_dialog(); return; }

    const char *pwd = lv_textarea_get_text(s_pwd_ta);
    do_connect(s_target_ssid, pwd);
    close_password_dialog();
}

static void pwd_cancel_cb(lv_event_t *e)
{
    close_password_dialog();
}

static void open_password_dialog(const char *ssid, wifi_auth_mode_t auth_mode)
{
    if (s_pwd_dialog) close_password_dialog();

    strncpy(s_target_ssid, ssid, sizeof(s_target_ssid) - 1);
    s_target_auth = auth_mode;

    s_pwd_dialog = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_pwd_dialog, 460, 280);
    lv_obj_align(s_pwd_dialog, LV_ALIGN_CENTER, 0, -50);
    lv_obj_set_style_bg_color(s_pwd_dialog, COLOR_BG, 0);
    lv_obj_set_style_radius(s_pwd_dialog, 15, 0);
    lv_obj_set_style_pad_all(s_pwd_dialog, 15, 0);

    /* 标题 */
    lv_obj_t *title = lv_label_create(s_pwd_dialog);
    char title_buf[64];
    snprintf(title_buf, sizeof(title_buf), "连接到: %s", ssid);
    lv_label_set_text(title, title_buf);
    lv_obj_set_style_text_font(title, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    /* 密码输入 */
    lv_obj_t *pwd_lbl = lv_label_create(s_pwd_dialog);
    lv_label_set_text(pwd_lbl, "密码:");
    lv_obj_set_style_text_font(pwd_lbl, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_pos(pwd_lbl, 10, 65);

    s_pwd_ta = lv_textarea_create(s_pwd_dialog);
    lv_obj_set_size(s_pwd_ta, 340, 40);
    lv_obj_set_pos(s_pwd_ta, 70, 60);
    lv_textarea_set_one_line(s_pwd_ta, true);
    lv_textarea_set_password_mode(s_pwd_ta, true);
    lv_textarea_set_placeholder_text(s_pwd_ta, "输入WiFi密码");
    lv_obj_set_style_text_font(s_pwd_ta, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_style_pad_all(s_pwd_ta, 4, 0);
    lv_obj_add_event_cb(s_pwd_ta, pwd_ta_focus_cb, LV_EVENT_FOCUSED, NULL);

    /* 连接按钮 */
    lv_obj_t *btn_connect = lv_button_create(s_pwd_dialog);
    lv_obj_set_size(btn_connect, 120, 40);
    lv_obj_set_pos(btn_connect, 80, 180);
    lv_obj_set_style_bg_color(btn_connect, COLOR_PRIMARY, 0);
    lv_obj_t *lbl_conn = lv_label_create(btn_connect);
    lv_label_set_text(lbl_conn, "连接");
    lv_obj_set_style_text_font(lbl_conn, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_style_text_color(lbl_conn, COLOR_BG, 0);
    lv_obj_align(lbl_conn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn_connect, pwd_connect_cb, LV_EVENT_CLICKED, NULL);

    /* 取消按钮 */
    lv_obj_t *btn_cancel = lv_button_create(s_pwd_dialog);
    lv_obj_set_size(btn_cancel, 120, 40);
    lv_obj_set_pos(btn_cancel, 250, 180);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x999999), 0);
    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "取消");
    lv_obj_set_style_text_font(lbl_cancel, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_style_text_color(lbl_cancel, COLOR_BG, 0);
    lv_obj_align(lbl_cancel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn_cancel, pwd_cancel_cb, LV_EVENT_CLICKED, NULL);

    /* 延迟绑定键盘 */
    s_deferred_kb_ta = s_pwd_ta;
    lv_timer_create(kb_bind_deferred_cb, 1, NULL);
}

static void close_password_dialog(void)
{
    if (guider_ui.g_kb_top_layer) {
        lv_keyboard_set_textarea(guider_ui.g_kb_top_layer, NULL);
        lv_obj_add_flag(guider_ui.g_kb_top_layer, LV_OBJ_FLAG_HIDDEN);
    }
    s_pwd_ta = NULL;
    if (s_pwd_dialog) {
        lv_obj_delete(s_pwd_dialog);
        s_pwd_dialog = NULL;
    }
}

/* ======================== WiFi 列表项事件 ======================== */

static void wifi_item_click_cb(lv_event_t *e)
{
    char *ssid = (char *)lv_event_get_user_data(e);
    if (!ssid || strlen(ssid) == 0) return;

    /* 已连接该网络 → 断开 */
    if (wifi_is_connected()) {
        const char *cur = wifi_get_connected_ssid();
        if (cur && strcmp(cur, ssid) == 0) {
            do_disconnect();
            start_scan();  /* 刷新列表 */
            return;
        }
    }

    /* 查找认证模式 */
    wifi_auth_mode_t auth = find_auth_mode(ssid);

    /* 开放网络直接连接 */
    if (auth == WIFI_AUTH_OPEN) {
        do_connect(ssid, "");
        return;
    }

    /* 加密网络弹出密码框 */
    open_password_dialog(ssid, auth);
}

/* ======================== WiFi 连接/断开 ======================== */

static void do_connect(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "Connecting to: %s", ssid);
    wifi_hosted_connect(ssid, password);
    wifi_hosted_save_credentials(ssid, password);

    if (s_status_label) {
        char buf[64];
        snprintf(buf, sizeof(buf), "正在连接: %s ...", ssid);
        lv_label_set_text(s_status_label, buf);
    }
}

static void do_disconnect(void)
{
    ESP_LOGI(TAG, "Disconnecting...");
    wifi_hosted_disconnect();
    wifi_hosted_clear_credentials();

    if (s_status_label) {
        lv_label_set_text(s_status_label, "已断开连接");
    }
    if (s_disconnect_btn) {
        lv_obj_add_flag(s_disconnect_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ======================== 扫描 ======================== */

static void start_scan(void)
{
    if (s_loading_label && lv_obj_is_valid(s_loading_label)) {
        lv_label_set_text(s_loading_label, "正在扫描...");
        lv_obj_remove_flag(s_loading_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_list_cont) {
        clear_wifi_list();
    }
    if (s_scan_btn) {
        lv_obj_add_state(s_scan_btn, LV_STATE_DISABLED);
    }

    wifi_hosted_scan_start(wifi_scan_callback);
}

static void wifi_scan_callback(wifi_ap_info_t *aps, int count)
{
    /* 此回调在 WiFi 事件任务上下文中，需要加锁操作 UI */
    lvgl_lock();

    if (s_loading_label && lv_obj_is_valid(s_loading_label)) {
        lv_obj_add_flag(s_loading_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_scan_btn && lv_obj_is_valid(s_scan_btn)) {
        lv_obj_clear_state(s_scan_btn, LV_STATE_DISABLED);
    }

    if (!s_list_cont || !lv_obj_is_valid(s_list_cont) || count <= 0 || !aps) {
        if (s_list_cont && lv_obj_is_valid(s_list_cont)) {
            lv_obj_t *empty = lv_label_create(s_list_cont);
            lv_obj_set_user_data(empty, (void*)1);
            lv_label_set_text(empty, count == 0 ? "未找到可用网络" : "扫描失败");
            lv_obj_set_style_text_font(empty, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
            lv_obj_set_style_text_color(empty, COLOR_TEXT_LIGHT, 0);
            lv_obj_align(empty, LV_ALIGN_CENTER, 0, 0);
        }
        lvgl_unlock();
        return;
    }

    /* 按信号强度排序（冒泡） */
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (aps[i].rssi < aps[j].rssi) {
                wifi_ap_info_t tmp = aps[i];
                aps[i] = aps[j];
                aps[j] = tmp;
            }
        }
    }

    /* 缓存扫描结果（用于密码对话框判断加密类型） */
    s_scan_cache_count = count < MAX_SCAN_CACHE ? count : MAX_SCAN_CACHE;
    memcpy(s_scan_cache, aps, sizeof(wifi_ap_info_t) * s_scan_cache_count);

    const char *connected_ssid = wifi_is_connected() ? wifi_get_connected_ssid() : NULL;

    int item_idx = 0;
    for (int i = 0; i < count; i++) {
        /* 跳过空 SSID（隐藏网络） */
        if (strlen(aps[i].ssid) == 0) continue;

        bool is_connected = (connected_ssid && strcmp(aps[i].ssid, connected_ssid) == 0);

        /* 创建列表项 */
        lv_obj_t *item = lv_obj_create(s_list_cont);
        lv_obj_set_size(item, PANEL_W - 60, ITEM_H);
        lv_obj_set_pos(item, 0, item_idx * (ITEM_H + ITEM_GAP));
        lv_obj_set_style_bg_color(item, is_connected ? lv_color_hex(0xe8f5e9) : COLOR_BG, 0);
        lv_obj_set_style_bg_opa(item, 255, 0);
        lv_obj_set_style_border_width(item, 1, 0);
        lv_obj_set_style_border_color(item, lv_color_hex(0xe0e0e0), 0);
        lv_obj_set_style_radius(item, 8, 0);
        lv_obj_set_style_pad_all(item, 4, 0);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);

        /* 为 item 分配并保存 ssid_copy（在清除列表时释放） */
        char *ssid_copy = lv_malloc(33);
        if (ssid_copy) {
            strncpy(ssid_copy, aps[i].ssid, 32);
            ssid_copy[32] = '\0';
            lv_obj_set_user_data(item, ssid_copy);
            lv_obj_add_event_cb(item, wifi_item_click_cb, LV_EVENT_CLICKED, ssid_copy);
        }

        /* SSID 标签 */
        lv_obj_t *ssid_lbl = lv_label_create(item);
        lv_obj_set_user_data(ssid_lbl, (void*)1);
        lv_label_set_text(ssid_lbl, aps[i].ssid);
        lv_obj_set_style_text_font(ssid_lbl, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
        lv_obj_set_style_text_color(ssid_lbl, COLOR_TEXT, 0);
        lv_obj_set_pos(ssid_lbl, 12, 8);

        /* 已连接标记 */
        if (is_connected) {
            lv_obj_t *conn_lbl = lv_label_create(item);
            lv_obj_set_user_data(conn_lbl, (void*)1);
            lv_label_set_text(conn_lbl, "已连接");
            lv_obj_set_style_text_font(conn_lbl, &lv_font_ZiTiQuanWeiJunHeiW22_16, 0);
            lv_obj_set_style_text_color(conn_lbl, COLOR_CONNECTED, 0);
            lv_obj_set_pos(conn_lbl, 12, 32);
        }

        item_idx++;
    }

    lvgl_unlock();
}

/* ======================== 主面板 ======================== */

static void panel_close_cb(lv_event_t *e)
{
    wifi_config_ui_close();
}

static void scan_btn_cb(lv_event_t *e)
{
    start_scan();
}

static void disconnect_btn_cb(lv_event_t *e)
{
    do_disconnect();
    start_scan();  /* 断开后刷新列表 */
}

void wifi_config_ui_open(void)
{
    if (s_panel) return; /* 已打开 */

    /* 半透明遮罩 */
    lv_obj_add_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(lv_layer_top(), LV_OPA_50, 0);
    lv_obj_set_style_bg_color(lv_layer_top(), lv_color_hex(0x000000), 0);

    /* 主面板 */
    s_panel = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_panel, PANEL_W, PANEL_H);
    lv_obj_align(s_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_panel, COLOR_BG, 0);
    lv_obj_set_style_radius(s_panel, 15, 0);
    lv_obj_set_style_pad_all(s_panel, 15, 0);

    /* 标题栏 */
    lv_obj_t *title = lv_label_create(s_panel);
    lv_label_set_text(title, "WiFi 设置");
    lv_obj_set_style_text_font(title, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT, 0);
    lv_obj_set_pos(title, 15, 10);

    /* 关闭按钮 */
    lv_obj_t *close_btn = lv_button_create(s_panel);
    lv_obj_set_size(close_btn, 40, 40);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -10, 5);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xe0e0e0), 0);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "X");
    lv_obj_set_style_text_font(close_lbl, &lv_font_montserratMedium_16, 0);
    lv_obj_set_style_text_color(close_lbl, COLOR_TEXT, 0);
    lv_obj_align(close_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(close_btn, panel_close_cb, LV_EVENT_CLICKED, NULL);

    /* 连接状态 */
    s_status_label = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_status_label, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_style_text_color(s_status_label, COLOR_TEXT, 0);
    lv_obj_set_pos(s_status_label, 15, 45);

    if (wifi_is_connected()) {
        const char *ssid = wifi_get_connected_ssid();
        char buf[64];
        snprintf(buf, sizeof(buf), "已连接: %s", ssid ? ssid : "");
        lv_label_set_text(s_status_label, buf);
    } else {
        lv_label_set_text(s_status_label, "未连接");
    }

    /* 操作按钮区 */
    s_scan_btn = lv_button_create(s_panel);
    lv_obj_set_size(s_scan_btn, 100, 36);
    lv_obj_set_pos(s_scan_btn, 15, 80);
    lv_obj_set_style_bg_color(s_scan_btn, COLOR_PRIMARY, 0);
    lv_obj_t *scan_lbl = lv_label_create(s_scan_btn);
    lv_label_set_text(scan_lbl, "扫描");
    lv_obj_set_style_text_font(scan_lbl, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_style_text_color(scan_lbl, COLOR_BG, 0);
    lv_obj_align(scan_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(s_scan_btn, scan_btn_cb, LV_EVENT_CLICKED, NULL);

    s_disconnect_btn = lv_button_create(s_panel);
    lv_obj_set_size(s_disconnect_btn, 120, 36);
    lv_obj_set_pos(s_disconnect_btn, 125, 80);
    lv_obj_set_style_bg_color(s_disconnect_btn, COLOR_DANGER, 0);
    lv_obj_t *disc_lbl = lv_label_create(s_disconnect_btn);
    lv_label_set_text(disc_lbl, "断开连接");
    lv_obj_set_style_text_font(disc_lbl, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_style_text_color(disc_lbl, COLOR_BG, 0);
    lv_obj_align(disc_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(s_disconnect_btn, disconnect_btn_cb, LV_EVENT_CLICKED, NULL);
    /* 未连接时隐藏断开按钮 */
    if (!wifi_is_connected()) {
        lv_obj_add_flag(s_disconnect_btn, LV_OBJ_FLAG_HIDDEN);
    }

    /* 分隔线 */
    lv_obj_t *line = lv_obj_create(s_panel);
    lv_obj_set_size(line, PANEL_W - 30, 2);
    lv_obj_set_pos(line, 15, 125);
    lv_obj_set_style_bg_color(line, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_bg_opa(line, 255, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);

    /* 可用网络标签 */
    lv_obj_t *avail_label = lv_label_create(s_panel);
    lv_label_set_text(avail_label, "可用网络");
    lv_obj_set_style_text_font(avail_label, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_style_text_color(avail_label, COLOR_TEXT_LIGHT, 0);
    lv_obj_set_pos(avail_label, 15, 132);

    /* WiFi 列表容器（可滚动） */
    s_list_cont = lv_obj_create(s_panel);
    lv_obj_set_size(s_list_cont, PANEL_W - 30, PANEL_H - 175);
    lv_obj_set_pos(s_list_cont, 15, 160);
    lv_obj_set_style_bg_color(s_list_cont, lv_color_hex(0xf5f5f5), 0);
    lv_obj_set_style_border_width(s_list_cont, 0, 0);
    lv_obj_set_style_radius(s_list_cont, 8, 0);
    lv_obj_set_style_pad_all(s_list_cont, 5, 0);
    lv_obj_set_scrollbar_mode(s_list_cont, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(s_list_cont, LV_OBJ_FLAG_SCROLL_ELASTIC);

    /* 加载提示 */
    s_loading_label = lv_label_create(s_list_cont);
    lv_obj_set_user_data(s_loading_label, (void*)1);
    lv_label_set_text(s_loading_label, "正在扫描...");
    lv_obj_set_style_text_font(s_loading_label, &lv_font_ZiTiQuanWeiJunHeiW22_25, 0);
    lv_obj_set_style_text_color(s_loading_label, COLOR_TEXT_LIGHT, 0);
    lv_obj_align(s_loading_label, LV_ALIGN_CENTER, 0, 0);

    /* 注册键盘关闭回调 */
    if (guider_ui.g_kb_top_layer) {
        lv_obj_add_event_cb(guider_ui.g_kb_top_layer, kb_dismiss_cb, LV_EVENT_ALL, NULL);
    }

    /* 自动开始扫描 */
    start_scan();
}

void wifi_config_ui_close(void)
{
    close_password_dialog();

    /* 释放列表中的动态内存和对象 */
    if (s_list_cont) {
        uint32_t cnt = lv_obj_get_child_count(s_list_cont);
        for (int32_t i = cnt - 1; i >= 0; i--) {
            lv_obj_t *child = lv_obj_get_child(s_list_cont, i);
            /* 如果 user_data 是 lv_malloc 分配的 ssid_copy 指针则释放 */
            void *ud = lv_obj_get_user_data(child);
            if (ud && ud != (void*)1) {
                lv_free(ud);
            }
            lv_obj_delete(child);
        }
    }

    s_list_cont = NULL;
    s_status_label = NULL;
    s_scan_btn = NULL;
    s_disconnect_btn = NULL;
    s_loading_label = NULL;
    s_scan_cache_count = 0;

    if (s_panel) {
        lv_obj_delete(s_panel);
        s_panel = NULL;
    }

    /* 移除遮罩 */
    lv_obj_remove_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(lv_layer_top(), LV_OPA_TRANSP, 0);
}

bool wifi_config_ui_is_open(void)
{
    return s_panel != NULL;
}

void wifi_config_ui_update_status(void)
{
    if (!s_panel || !s_status_label) return;

    lvgl_lock();

    if (wifi_is_connected()) {
        const char *ssid = wifi_get_connected_ssid();
        char buf[64];
        snprintf(buf, sizeof(buf), "已连接: %s", ssid ? ssid : "");
        lv_label_set_text(s_status_label, buf);
        if (s_disconnect_btn) lv_obj_remove_flag(s_disconnect_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(s_status_label, "未连接");
        if (s_disconnect_btn) lv_obj_add_flag(s_disconnect_btn, LV_OBJ_FLAG_HIDDEN);
    }

    lvgl_unlock();
}

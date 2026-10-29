/*
 * UI 管理模块
 * 负责：UI 页面管理、页面切换、UI 资源管理
 */
#include "ui_manager.h"
#include "bsp/display_init.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <stdio.h>

static const char *TAG = "ui_manager";

/* 全局屏幕引用 */
static lv_obj_t *s_main_screen = NULL;

/* 亮度 NVS 存取（与 custom.c 共用同一个 NVS key） */
#define BRIGHTNESS_NVS_NS   "display"
#define BRIGHTNESS_NVS_KEY  "brightness"

static void ui_brightness_save_nvs(int val)
{
    nvs_handle_t nvs;
    if (nvs_open(BRIGHTNESS_NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_i8(nvs, BRIGHTNESS_NVS_KEY, (int8_t)val);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static int ui_brightness_load_nvs(void)
{
    nvs_handle_t nvs;
    int8_t val = 50;
    if (nvs_open(BRIGHTNESS_NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_i8(nvs, BRIGHTNESS_NVS_KEY, &val);
        nvs_close(nvs);
    }
    return (int)val;
}

/* 亮度滑块事件回调 */
static void brightness_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    display_brightness_set(val);
    ui_brightness_save_nvs(val);
}

lv_obj_t *ui_manager_init(void)
{
    s_main_screen = lv_screen_active();
    ESP_LOGI(TAG, "UI manager initialized");
    return s_main_screen;
}

void ui_init(void)
{
	//初始化屏幕
    lv_obj_t *scr = ui_manager_init();
    // 整个屏幕大小1024*600
    lv_obj_set_size(scr, 1024, 600);
    //设置偏灰色的背景色
    lv_obj_set_style_bg_color(scr, lv_color_lighten(lv_palette_main(LV_PALETTE_GREY) , 10), LV_PART_MAIN);

    // 创建一个1024*50的对象，作为顶部导航栏
    lv_obj_t * obj = lv_obj_create(scr);
    // 设置对象大小和位置
    lv_obj_set_size(obj, 1024, 50);
    // 顶部位置
    lv_obj_set_pos(obj, 0, 0);
    // 设置成蓝色
    lv_obj_set_style_bg_color(obj, lv_color_lighten(lv_palette_main(LV_PALETTE_BLUE) , 50), LV_PART_MAIN);

    // ====== 亮度设置区域 ======
    // 初始化亮度 PWM
    display_brightness_init();

    // 创建亮度设置容器
    lv_obj_t *bright_cont = lv_obj_create(scr);
    lv_obj_set_size(bright_cont, 300, 80);
    lv_obj_set_pos(bright_cont, 362, 250);
    lv_obj_set_style_bg_color(bright_cont, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(bright_cont, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(bright_cont, 10, LV_PART_MAIN);

    // 亮度标签
    lv_obj_t *bright_label = lv_label_create(bright_cont);
    lv_label_set_text(bright_label, "屏幕亮度");
    lv_obj_set_style_text_font(bright_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(bright_label, LV_ALIGN_TOP_MID, 0, 5);

    // 亮度滑块
    lv_obj_t *bright_slider = lv_slider_create(bright_cont);
    lv_slider_set_range(bright_slider, 0, 100);
    // 从 NVS 恢复亮度（默认 50%）
    int saved_bright = ui_brightness_load_nvs();
    lv_slider_set_value(bright_slider, saved_bright, LV_ANIM_OFF);
    lv_obj_set_width(bright_slider, 250);
    lv_obj_align(bright_slider, LV_ALIGN_CENTER, 0, 10);
    lv_obj_add_event_cb(bright_slider, brightness_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // 设置初始亮度（从 NVS 恢复）
    display_brightness_set(saved_bright);
    ESP_LOGI(TAG, "Boot brightness restored to %d%%", saved_bright);
}
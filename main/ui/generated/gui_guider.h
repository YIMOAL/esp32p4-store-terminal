/*
* Copyright 2026 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#ifndef GUI_GUIDER_H
#define GUI_GUIDER_H
#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"


typedef struct
{
  
	lv_obj_t *screen;
	bool screen_del;
	lv_obj_t *screen_cont_title;
	lv_obj_t *screen_soc_titie;
	lv_obj_t *screen_soc_logo;
	lv_obj_t *screen_btn_stock;
	lv_obj_t *screen_btn_stock_label;
	lv_obj_t *screen_btn_sell;
	lv_obj_t *screen_btn_sell_label;
	lv_obj_t *screen_btn_check;
	lv_obj_t *screen_btn_check_label;
	lv_obj_t *screen_btn_set;
	lv_obj_t *screen_btn_set_label;
	lv_obj_t *screen_cont_time;
	lv_obj_t *screen_digital_clock_1;
	lv_obj_t *screen_datetext_1;
	lv_obj_t *screen_img_4;
	lv_obj_t *screen_img_5;
	lv_obj_t *screen_img_1;
	lv_obj_t *screen_label_2;
	lv_obj_t *screen_label_3;
	lv_obj_t *screen_label_4;
	lv_obj_t *screen_label_5;
	lv_obj_t *screen_cont_inventory;
	lv_obj_t *screen_label_7;
	lv_obj_t *screen_label_6;
	lv_obj_t *screen_img_2;
	lv_obj_t *screen_img_3;
	lv_obj_t *screen_stock;
	bool screen_stock_del;
	lv_obj_t *screen_stock_btn_1;
	lv_obj_t *screen_stock_btn_1_label;
	lv_obj_t *screen_stock_cont_1;
	lv_obj_t *screen_stock_cont_shop;
	lv_obj_t *screen_stock_label_1;
	lv_obj_t *screen_stock_img_1;
	lv_obj_t *screen_stock_btn_3;
	lv_obj_t *screen_stock_btn_3_label;
	lv_obj_t *screen_stock_btn_4;
	lv_obj_t *screen_stock_btn_4_label;
	lv_obj_t *screen_stock_cont_liebiao;
	lv_obj_t *screen_stock_label_4;
	lv_obj_t *screen_stock_label_2;
	lv_obj_t *screen_stock_label_3;
	lv_obj_t *screen_stock_btn_2;
	lv_obj_t *screen_stock_btn_2_label;
	lv_obj_t *screen_stock_btn_5;
	lv_obj_t *screen_stock_btn_5_label;
	lv_obj_t *screen_stock_btn_6;
	lv_obj_t *screen_stock_btn_6_label;
	lv_obj_t *screen_sell;
	bool screen_sell_del;
	lv_obj_t *screen_sell_btn_1;
	lv_obj_t *screen_sell_btn_1_label;
	lv_obj_t *screen_sell_cont_1;
	lv_obj_t *screen_sell_label_1;
	lv_obj_t *screen_sell_img_1;
	lv_obj_t *screen_sell_btn_3;
	lv_obj_t *screen_sell_btn_3_label;
	lv_obj_t *screen_sell_btn_4;
	lv_obj_t *screen_sell_btn_4_label;
	lv_obj_t *screen_sell_cont_shop;
	lv_obj_t *screen_sell_cont_4;
	lv_obj_t *screen_sell_cont_2;
	lv_obj_t *screen_sell_label_4;
	lv_obj_t *screen_sell_label_3;
	lv_obj_t *screen_sell_label_2;
	lv_obj_t *screen_sell_btn_7;
	lv_obj_t *screen_sell_btn_7_label;
	lv_obj_t *screen_sell_btn_6;
	lv_obj_t *screen_sell_btn_6_label;
	lv_obj_t *screen_sell_btn_5;
	lv_obj_t *screen_sell_btn_5_label;
	lv_obj_t *screen_check;
	bool screen_check_del;
	lv_obj_t *screen_check_btn_turn;
	lv_obj_t *screen_check_btn_turn_label;
	lv_obj_t *screen_check_label_1;
	lv_obj_t *screen_check_line_1;
	lv_obj_t *screen_check_label_2;
	lv_obj_t *screen_check_btn_2;
	lv_obj_t *screen_check_btn_2_label;
	lv_obj_t *screen_check_btn_3;
	lv_obj_t *screen_check_btn_3_label;
	lv_obj_t *screen_check_label_3;
	lv_obj_t *screen_check_btn_4;
	lv_obj_t *screen_check_btn_4_label;
	lv_obj_t *screen_check_label_4;
	lv_obj_t *screen_check_line_2;
	lv_obj_t *screen_check_line_3;
	lv_obj_t *screen_check_line_4;
	lv_obj_t *screen_check_line_5;
	lv_obj_t *screen_check_btn_5;
	lv_obj_t *screen_check_btn_5_label;
	lv_obj_t *screen_check_label_5;
	lv_obj_t *screen_check_label_6;
	lv_obj_t *screen_check_label_7;
	lv_obj_t *screen_check_label_8;
	lv_obj_t *screen_check_label_9;
	lv_obj_t *screen_check_cont_1;
	lv_obj_t *screen_check_search_1;
	lv_obj_t *screen_check_btn_6;
	lv_obj_t *screen_check_btn_6_label;
	lv_obj_t *screen_check_btn_7;
	lv_obj_t *screen_check_btn_7_label;
	lv_obj_t *screen_check_img_1;
	lv_obj_t *screen_set;
	bool screen_set_del;
	lv_obj_t *screen_set_btn_1;
	lv_obj_t *screen_set_btn_1_label;
	lv_obj_t *screen_set_slider_1;
	lv_obj_t *screen_set_img_1;
	lv_obj_t *screen_set_label_1;
	lv_obj_t *screen_set_img_2;
	lv_obj_t *screen_set_label_2;
	lv_obj_t *screen_set_sw_1;
	lv_obj_t *g_kb_top_layer;
}lv_ui;

typedef void (*ui_setup_scr_t)(lv_ui * ui);

void ui_init_style(lv_style_t * style);

void ui_load_scr_animation(lv_ui *ui, lv_obj_t ** new_scr, bool new_scr_del, bool * old_scr_del, ui_setup_scr_t setup_scr,
                           lv_screen_load_anim_t anim_type, uint32_t time, uint32_t delay, bool is_clean, bool auto_del);

void ui_animation(void * var, uint32_t duration, int32_t delay, int32_t start_value, int32_t end_value, lv_anim_path_cb_t path_cb,
                  uint32_t repeat_cnt, uint32_t repeat_delay, uint32_t playback_time, uint32_t playback_delay,
                  lv_anim_exec_xcb_t exec_cb, lv_anim_start_cb_t start_cb, lv_anim_completed_cb_t ready_cb, lv_anim_deleted_cb_t deleted_cb);


void init_scr_del_flag(lv_ui *ui);

void setup_bottom_layer(void);

void setup_ui(lv_ui *ui);

void video_play(lv_ui *ui);

void init_keyboard(lv_ui *ui);

extern lv_ui guider_ui;


void setup_scr_screen(lv_ui *ui);
void setup_scr_screen_stock(lv_ui *ui);
void setup_scr_screen_sell(lv_ui *ui);
void setup_scr_screen_check(lv_ui *ui);
void setup_scr_screen_set(lv_ui *ui);
LV_IMAGE_DECLARE(_ca01ce804459917214bfe036e876917e_RGB565A8_310x60);

LV_IMAGE_DECLARE(_2_RGB565A8_190x190);

LV_IMAGE_DECLARE(_3_RGB565A8_190x190);

LV_IMAGE_DECLARE(_1_RGB565A8_190x190);

LV_IMAGE_DECLARE(_4_RGB565A8_190x190);
LV_IMAGE_DECLARE(_wifi_RGB565A8_30x30);
LV_IMAGE_DECLARE(_MQTT_RGB565A8_30x30);
LV_IMAGE_DECLARE(_logo_RGB565A8_45x45);
LV_IMAGE_DECLARE(_5_RGB565A8_100x100);
LV_IMAGE_DECLARE(_6_RGB565A8_100x100);
LV_IMAGE_DECLARE(_14_RGB565A8_35x35);

LV_IMAGE_DECLARE(_10_RGB565A8_90x90);

LV_IMAGE_DECLARE(_18_RGB565A8_90x90);

LV_IMAGE_DECLARE(_17_RGB565A8_90x90);
LV_IMAGE_DECLARE(_15_RGB565A8_50x50);

LV_IMAGE_DECLARE(_7_RGB565A8_70x70);

LV_IMAGE_DECLARE(_8_RGB565A8_70x70);

LV_IMAGE_DECLARE(_9_RGB565A8_70x70);

LV_IMAGE_DECLARE(_11_RGB565A8_70x70);
LV_IMAGE_DECLARE(_13_RGB565A8_28x28);
LV_IMAGE_DECLARE(_16_RGB565A8_50x50);
LV_IMAGE_DECLARE(_wifi_RGB565A8_50x50);

LV_FONT_DECLARE(lv_font_ZiTiQuanWeiJunHeiW22_25)
LV_FONT_DECLARE(lv_font_ZiTiQuanWeiJunHeiW22_28)
LV_FONT_DECLARE(lv_font_Acme_Regular_50)
LV_FONT_DECLARE(lv_font_montserratMedium_16)
LV_FONT_DECLARE(lv_font_ZiTiQuanWeiJunHeiW22_16)
LV_FONT_DECLARE(lv_font_ZiTiQuanWeiJunHeiW22_18)
LV_FONT_DECLARE(lv_font_ZiTiQuanWeiJunHeiW22_35)
LV_FONT_DECLARE(lv_font_ZiTiQuanWeiJunHeiW22_22)
LV_FONT_DECLARE(lv_font_ZiTiQuanWeiJunHeiW22_20)


#ifdef __cplusplus
}
#endif
#endif

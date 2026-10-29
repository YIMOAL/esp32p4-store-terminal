/*
* Copyright 2026 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#include "events_init.h"
#include <stdio.h>
#include "lvgl.h"

#if LV_USE_GUIDER_SIMULATOR && LV_USE_FREEMASTER
#include "freemaster_client.h"
#endif


static void screen_btn_stock_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_CLICKED:
    {
        ui_load_scr_animation(&guider_ui, &guider_ui.screen_stock, guider_ui.screen_stock_del, &guider_ui.screen_del, setup_scr_screen_stock, LV_SCR_LOAD_ANIM_FADE_ON, 10, 10, false, false);
        break;
    }
    default:
        break;
    }
}

static void screen_btn_sell_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_CLICKED:
    {
        ui_load_scr_animation(&guider_ui, &guider_ui.screen_sell, guider_ui.screen_sell_del, &guider_ui.screen_del, setup_scr_screen_sell, LV_SCR_LOAD_ANIM_FADE_ON, 10, 10, false, false);
        break;
    }
    default:
        break;
    }
}

static void screen_btn_check_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_CLICKED:
    {
        ui_load_scr_animation(&guider_ui, &guider_ui.screen_check, guider_ui.screen_check_del, &guider_ui.screen_del, setup_scr_screen_check, LV_SCR_LOAD_ANIM_FADE_ON, 10, 10, false, false);
        break;
    }
    default:
        break;
    }
}

static void screen_btn_set_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_CLICKED:
    {
        ui_load_scr_animation(&guider_ui, &guider_ui.screen_set, guider_ui.screen_set_del, &guider_ui.screen_del, setup_scr_screen_set, LV_SCR_LOAD_ANIM_FADE_ON, 10, 10, false, false);
        break;
    }
    default:
        break;
    }
}

void events_init_screen (lv_ui *ui)
{
    lv_obj_add_event_cb(ui->screen_btn_stock, screen_btn_stock_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->screen_btn_sell, screen_btn_sell_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->screen_btn_check, screen_btn_check_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->screen_btn_set, screen_btn_set_event_handler, LV_EVENT_ALL, ui);
}

static void screen_stock_btn_1_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_CLICKED:
    {
        ui_load_scr_animation(&guider_ui, &guider_ui.screen, guider_ui.screen_del, &guider_ui.screen_stock_del, setup_scr_screen, LV_SCR_LOAD_ANIM_FADE_ON, 10, 10, false, false);
        break;
    }
    default:
        break;
    }
}

static void screen_stock_btn_2_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_CLICKED:
    {
        break;
    }
    default:
        break;
    }
}

void events_init_screen_stock (lv_ui *ui)
{
    lv_obj_add_event_cb(ui->screen_stock_btn_1, screen_stock_btn_1_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->screen_stock_btn_2, screen_stock_btn_2_event_handler, LV_EVENT_ALL, ui);
}

static void screen_sell_btn_1_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_CLICKED:
    {
        ui_load_scr_animation(&guider_ui, &guider_ui.screen, guider_ui.screen_del, &guider_ui.screen_sell_del, setup_scr_screen, LV_SCR_LOAD_ANIM_FADE_ON, 10, 10, false, false);
        break;
    }
    default:
        break;
    }
}

void events_init_screen_sell (lv_ui *ui)
{
    lv_obj_add_event_cb(ui->screen_sell_btn_1, screen_sell_btn_1_event_handler, LV_EVENT_ALL, ui);
}

static void screen_check_btn_turn_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_CLICKED:
    {
        ui_load_scr_animation(&guider_ui, &guider_ui.screen, guider_ui.screen_del, &guider_ui.screen_check_del, setup_scr_screen, LV_SCR_LOAD_ANIM_FADE_ON, 10, 10, false, false);
        break;
    }
    default:
        break;
    }
}

static void screen_check_search_1_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_CLICKED:
    {
        break;
    }
    default:
        break;
    }
}

void events_init_screen_check (lv_ui *ui)
{
    lv_obj_add_event_cb(ui->screen_check_btn_turn, screen_check_btn_turn_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->screen_check_search_1, screen_check_search_1_event_handler, LV_EVENT_ALL, ui);
}

static void screen_set_btn_1_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_CLICKED:
    {
        ui_load_scr_animation(&guider_ui, &guider_ui.screen, guider_ui.screen_del, &guider_ui.screen_set_del, setup_scr_screen, LV_SCR_LOAD_ANIM_FADE_ON, 10, 10, false, false);
        break;
    }
    default:
        break;
    }
}

void events_init_screen_set (lv_ui *ui)
{
    lv_obj_add_event_cb(ui->screen_set_btn_1, screen_set_btn_1_event_handler, LV_EVENT_ALL, ui);
}


void events_init(lv_ui *ui)
{

}

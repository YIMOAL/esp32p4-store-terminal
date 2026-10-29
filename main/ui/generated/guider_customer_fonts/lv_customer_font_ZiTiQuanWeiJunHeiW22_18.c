/*******************************************************************************
 * Size: 18 px
 * Bpp: 4
 * Opts: undefined
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef LV_CUSTOMER_FONT_ZITIQUANWEIJUNHEIW22_18
#define LV_CUSTOMER_FONT_ZITIQUANWEIJUNHEIW22_18 1
#endif

#if LV_CUSTOMER_FONT_ZITIQUANWEIJUNHEIW22_18

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+7EA2 "红" */
    0x0, 0x0, 0x75, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x3, 0xff, 0x20, 0x11, 0x11, 0x11,
    0x11, 0x0, 0x0, 0xd, 0xf6, 0x3, 0xff, 0xff,
    0xff, 0xff, 0x80, 0x0, 0x9f, 0xa0, 0x55, 0xff,
    0xff, 0xff, 0xff, 0x70, 0x6, 0xfe, 0x24, 0xff,
    0x10, 0xd, 0xf2, 0x0, 0x0, 0x5f, 0xff, 0xff,
    0xf8, 0x0, 0xd, 0xf2, 0x0, 0x0, 0x4f, 0xff,
    0xff, 0xc0, 0x0, 0xd, 0xf2, 0x0, 0x0, 0x3,
    0x17, 0xfe, 0x10, 0x0, 0xd, 0xf2, 0x0, 0x0,
    0x0, 0x4f, 0xf3, 0x0, 0x0, 0xd, 0xf2, 0x0,
    0x0, 0x6, 0xff, 0xed, 0xf9, 0x0, 0xd, 0xf2,
    0x0, 0x0, 0xd, 0xff, 0xff, 0xf8, 0x0, 0xd,
    0xf2, 0x0, 0x0, 0x8, 0xa8, 0x53, 0x0, 0x0,
    0xd, 0xf2, 0x0, 0x0, 0x0, 0x0, 0x25, 0x8a,
    0x30, 0xd, 0xf2, 0x0, 0x0, 0x49, 0xcf, 0xff,
    0xff, 0x40, 0xd, 0xf2, 0x0, 0x0, 0x7f, 0xff,
    0xfe, 0xbf, 0xff, 0xff, 0xff, 0xff, 0xf3, 0x3a,
    0x74, 0x10, 0xe, 0xff, 0xff, 0xff, 0xff, 0xf3
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 288, .box_w = 18, .box_h = 16, .ofs_x = 0, .ofs_y = -2}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32418, .range_length = 1, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t lv_customer_font_ZiTiQuanWeiJunHeiW22_18 = {
#else
lv_font_t lv_customer_font_ZiTiQuanWeiJunHeiW22_18 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 18,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -2,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if LV_CUSTOMER_FONT_ZITIQUANWEIJUNHEIW22_18*/


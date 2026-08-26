/*******************************************************************************
 * Size: 12 px
 * Bpp: 1
 * Opts: --font D:/git/esp32/assets/fonts/NotoSansSC-Regular.otf --symbols 语音助手系统设置 --size 12 --bpp 1 --format lvgl --no-compress --no-prefilter --no-kerning --lv-font-name lv_font_home_12 -o D:/git/esp32/main/lv_font_home_12.c
 ******************************************************************************/

#include "lvgl.h"

#ifndef LV_FONT_HOME_12
#define LV_FONT_HOME_12 1
#endif

#if LV_FONT_HOME_12

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+52A9 "助" */
    0x79, 0x9, 0x21, 0x3f, 0xbf, 0xf4, 0x92, 0x92,
    0x5e, 0x4a, 0x51, 0x5e, 0x3c, 0xc4, 0x37, 0x0,

    /* U+624B "手" */
    0x1, 0xcf, 0xc0, 0x10, 0x3f, 0xe0, 0x40, 0x8,
    0x3f, 0xf8, 0x20, 0x4, 0x0, 0x80, 0x70, 0x0,

    /* U+7CFB "系" */
    0x1, 0x8f, 0xc0, 0x44, 0x3f, 0x0, 0xd8, 0x61,
    0x1f, 0xd0, 0x28, 0x24, 0x98, 0x88, 0x70, 0x0,

    /* U+7EDF "统" */
    0x11, 0x4, 0x20, 0x9f, 0xa4, 0x87, 0x24, 0x4f,
    0x56, 0xa3, 0x14, 0xa, 0xae, 0x94, 0x33, 0x80,

    /* U+7F6E "置" */
    0x7f, 0xca, 0x48, 0xb6, 0x7f, 0xf0, 0x80, 0x6f,
    0xf, 0xe1, 0xfc, 0x3f, 0x84, 0x13, 0xff, 0x80,

    /* U+8BBE "设" */
    0x63, 0xc3, 0x24, 0x2, 0x4e, 0x47, 0x28, 0x2,
    0x7e, 0x24, 0x22, 0x24, 0x39, 0x82, 0x3c, 0xc,
    0x30,

    /* U+8BED "语" */
    0x4f, 0xe4, 0x40, 0x3f, 0x71, 0x2e, 0x24, 0x5f,
    0xc8, 0x1, 0x3f, 0x28, 0x27, 0x4, 0x9f, 0x80,

    /* U+97F3 "音" */
    0x4, 0xf, 0xf8, 0x42, 0x8, 0x8f, 0xfe, 0x0,
    0xf, 0xe1, 0x4, 0x3f, 0x84, 0x10, 0xfe, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 192, .box_w = 11, .box_h = 11, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 16, .adv_w = 192, .box_w = 11, .box_h = 11, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 32, .adv_w = 192, .box_w = 11, .box_h = 11, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 48, .adv_w = 192, .box_w = 11, .box_h = 11, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 64, .adv_w = 192, .box_w = 11, .box_h = 11, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 80, .adv_w = 192, .box_w = 12, .box_h = 11, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 97, .adv_w = 192, .box_w = 11, .box_h = 11, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 113, .adv_w = 192, .box_w = 11, .box_h = 11, .ofs_x = 0, .ofs_y = -1}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0xfa2, 0x2a52, 0x2c36, 0x2cc5, 0x3915, 0x3944, 0x454a
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 21161, .range_length = 17739, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 8, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
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
    .bpp = 1,
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
const lv_font_t lv_font_home_12 = {
#else
lv_font_t lv_font_home_12 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 11,          /*The maximum line height required by the font*/
    .base_line = 1,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if LV_FONT_HOME_12*/


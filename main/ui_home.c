#include "ui_home.h"
#include "esp32_voice.h"
#include "wifi_mgr.h"
#include "system_app.h"
#include "voice_app.h"
#include "font_mgr.h"

#include "lvgl.h"

/* 外置字库尚未烧录时，使用内置常用中文字体降级显示。 */
extern const lv_font_t lv_font_source_han_sans_sc_16_cjk;

static const lv_font_t *ui_font(void)
{
    const lv_font_t *font = font_mgr_get();
    return font ? font : &lv_font_source_han_sans_sc_16_cjk;
}

static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_wifi_label = NULL;
static lv_obj_t *g_reddot = NULL;       /* 系统设置图标上的红点 */
static lv_obj_t *g_sys_btn = NULL;

static void on_voice_click(lv_event_t *e)  { voice_app_open(); }
static void on_sys_click(lv_event_t *e)    { system_app_open(); }

typedef struct {
    const char *name;
    const char *symbol;
    uint32_t color;
    lv_event_cb_t on_click;
} home_app_t;

/* 新增桌面入口时，只需在此表添加一项，三列网格会自动排列并支持纵向滚动。 */
static const home_app_t s_home_apps[] = {
    { "语音助手", LV_SYMBOL_AUDIO,    0x2f80ed, on_voice_click },
    { "系统设置", LV_SYMBOL_SETTINGS, 0x6b7280, on_sys_click },
};

static lv_obj_t *make_icon(lv_obj_t *parent, const home_app_t *app)
{
    /* 触控单元：图标在上、名称在下，避免原有的大色块按钮。 */
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 56, 86);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, app->on_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *icon_bg = lv_obj_create(btn);
    lv_obj_set_size(icon_bg, 54, 54);
    lv_obj_set_style_radius(icon_bg, 16, 0);
    lv_obj_set_style_bg_color(icon_bg, lv_color_hex(app->color), 0);
    lv_obj_set_style_border_width(icon_bg, 0, 0);
    lv_obj_set_style_shadow_width(icon_bg, 8, 0);
    lv_obj_set_style_shadow_color(icon_bg, lv_color_hex(app->color), 0);
    lv_obj_set_style_shadow_opa(icon_bg, LV_OPA_30, 0);
    lv_obj_clear_flag(icon_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(icon_bg, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *symbol = lv_label_create(icon_bg);
    lv_label_set_text(symbol, app->symbol);
    lv_obj_set_style_text_font(symbol, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(symbol, lv_color_white(), 0);
    lv_obj_center(symbol);

    lv_obj_t *name = lv_label_create(btn);
    lv_label_set_text(name, app->name);
    lv_obj_set_width(name, LV_PCT(100));
    lv_obj_set_style_text_font(name, ui_font(), 0);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(0x263238), 0);
    lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, 0);
    return btn;
}

void ui_home_show(void)
{
    if (g_screen) {
        lv_screen_load(g_screen);
        ui_home_refresh();
        return;
    }

    g_screen = lv_obj_create(NULL);
    lv_obj_set_style_text_font(g_screen, ui_font(), 0);
    lv_obj_set_flex_flow(g_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(g_screen, 16, 0);

    /* 顶部状态栏 */
    lv_obj_t *bar = lv_obj_create(g_screen);
    lv_obj_set_width(bar, LV_PCT(100));
    lv_obj_set_height(bar, 40);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    g_wifi_label = lv_label_create(bar);
    lv_obj_set_style_text_font(g_wifi_label, ui_font(), 0);
    lv_label_set_text(g_wifi_label, "WiFi: 未连接");
    lv_obj_align(g_wifi_label, LV_ALIGN_LEFT_MID, 8, 0);

    /* 图标网格 */
    lv_obj_t *grid = lv_obj_create(g_screen);
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(grid, 12, 0);
    lv_obj_set_style_pad_row(grid, 14, 0);
    lv_obj_set_style_pad_column(grid, 6, 0);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);

    for (size_t i = 0; i < sizeof(s_home_apps) / sizeof(s_home_apps[0]); ++i) {
        lv_obj_t *app_btn = make_icon(grid, &s_home_apps[i]);
        if (s_home_apps[i].on_click == on_sys_click) {
            g_sys_btn = app_btn;
        }
    }

    /* 系统设置红点 */
    g_reddot = lv_obj_create(g_sys_btn);
    lv_obj_set_size(g_reddot, 18, 18);
    lv_obj_set_style_radius(g_reddot, 9, 0);
    lv_obj_set_style_bg_color(g_reddot, lv_color_hex(0xe53e3e), 0);
    lv_obj_set_style_border_width(g_reddot, 0, 0);
    lv_obj_align(g_reddot, LV_ALIGN_TOP_RIGHT, -6, 6);
    lv_obj_add_flag(g_reddot, LV_OBJ_FLAG_HIDDEN);

    lv_screen_load(g_screen);
    ui_home_refresh();
}

void ui_home_refresh(void)
{
    if (g_wifi_label) {
        if (wifi_mgr_state() == WIFI_ST_CONNECTED)
            lv_label_set_text(g_wifi_label, "WiFi: 已连接");
        else if (wifi_mgr_state() == WIFI_ST_CONNECTING)
            lv_label_set_text(g_wifi_label, "WiFi: 连接中");
        else
            lv_label_set_text(g_wifi_label, "WiFi: 未连接");
    }
    if (g_reddot) {
        if (g_ota_update_available)
            lv_obj_clear_flag(g_reddot, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(g_reddot, LV_OBJ_FLAG_HIDDEN);
    }
}

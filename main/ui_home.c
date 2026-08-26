#include "ui_home.h"
#include "esp32_voice.h"
#include "wifi_mgr.h"
#include "system_app.h"
#include "ui_chrome.h"
#include "voice_app.h"
#include "font_mgr.h"

#include "lvgl.h"

/* 外置字库尚未烧录时，使用内置常用中文字体降级显示。 */
extern const lv_font_t lv_font_source_han_sans_sc_16_cjk;
extern const lv_font_t lv_font_home_12;

static const lv_font_t *ui_font(void)
{
    const lv_font_t *font = font_mgr_get();
    return font ? font : &lv_font_source_han_sans_sc_16_cjk;
}

static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_wifi_label = NULL;
static lv_obj_t *g_wifi_dot = NULL;
static lv_obj_t *g_battery_label = NULL;
static lv_obj_t *g_reddot = NULL;       /* 系统设置图标上的红点 */
static lv_obj_t *g_sys_btn = NULL;
static lv_timer_t *g_status_timer = NULL;

static void on_voice_click(lv_event_t *e)  { (void)e; voice_app_open(); }
static void on_sys_click(lv_event_t *e)    { (void)e; system_app_open(); }
static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    ui_home_refresh();
}

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
    /* 手机式图标单元：仅保留图标本身，名称独立放在下方。 */
    lv_obj_t *btn = lv_button_create(parent);
    /* 240px 屏幕扣除主页内边距后仍需容纳三列。 */
    lv_obj_set_size(btn, 60, 86);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, app->on_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *icon_bg = lv_obj_create(btn);
    lv_obj_set_size(icon_bg, 58, 58);
    lv_obj_set_style_radius(icon_bg, 18, 0);
    lv_obj_set_style_bg_color(icon_bg, lv_color_hex(app->color), 0);
    lv_obj_set_style_border_width(icon_bg, 0, 0);
    lv_obj_set_style_shadow_width(icon_bg, 0, 0);
    lv_obj_clear_flag(icon_bg, LV_OBJ_FLAG_SCROLLABLE);
    /* 触点命中图标子对象时，将事件冒泡到应用按钮。 */
    lv_obj_add_flag(icon_bg, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_align(icon_bg, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *symbol = lv_label_create(icon_bg);
    lv_label_set_text(symbol, app->symbol);
    /* LVGL 内置符号字体包含应用图标，中文名称另用外置中文字库。 */
    lv_obj_set_style_text_font(symbol, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(symbol, lv_color_white(), 0);
    lv_obj_add_flag(symbol, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_center(symbol);

    lv_obj_t *name = lv_label_create(btn);
    lv_label_set_text(name, app->name);
    /* 主页标签采用原生 12px 点阵字体，不进行缩放，保证笔画清晰。 */
    lv_obj_set_width(name, 60);
    lv_obj_set_height(name, 16);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_font(name, &lv_font_home_12, 0);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(0x263238), 0);
    lv_obj_add_flag(name, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 65);
    return btn;
}

void ui_home_show(void)
{
    if (g_screen) {
        lv_screen_load(g_screen);
        ui_home_refresh();
        return;
    }

    g_screen = ui_chrome_screen_create();
    ui_chrome_add_status(g_screen);
    /* 主页没有返回按钮，但同样使用公共状态栏，内容从 y=20 开始。 */
    lv_obj_t *grid = lv_obj_create(g_screen);
    lv_obj_set_size(grid, LV_PCT(100), 320 - UI_STATUS_BAR_HEIGHT);
    lv_obj_set_pos(grid, 0, UI_STATUS_BAR_HEIGHT);
    /* 列式父容器下必须显式占满可用宽度，否则网格会按内容收缩为单列。 */
    /* 三列固定格宽：避免内容宽度导致入口退化为纵向单列。 */
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_shadow_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 4, 0);
    lv_obj_set_style_pad_row(grid, 12, 0);
    lv_obj_set_style_pad_column(grid, 10, 0);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_OFF);

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
    /* WiFi 事件运行在系统任务中，主页仅由 LVGL 定时器读取状态并刷新。 */
    if (!g_status_timer) {
        g_status_timer = lv_timer_create(status_timer_cb, 1000, NULL);
    }
}

void ui_home_refresh(void)
{
    if (g_wifi_label && g_wifi_dot) {
        wifi_state_t state = wifi_mgr_state();
        if (state == WIFI_ST_CONNECTED) {
            lv_obj_set_style_text_color(g_wifi_label, lv_color_hex(0x2f80ed), 0);
            lv_obj_set_style_bg_color(g_wifi_dot, lv_color_hex(0x2f80ed), 0);
        } else if (state == WIFI_ST_CONNECTING) {
            lv_obj_set_style_text_color(g_wifi_label, lv_color_hex(0xefaa44), 0);
            lv_obj_set_style_bg_color(g_wifi_dot, lv_color_hex(0xefaa44), 0);
        } else {
            lv_obj_set_style_text_color(g_wifi_label, lv_color_hex(0x9aa5b7), 0);
            lv_obj_set_style_bg_color(g_wifi_dot, lv_color_hex(0x9aa5b7), 0);
        }
    }
    if (g_reddot) {
        if (g_ota_update_available)
            lv_obj_clear_flag(g_reddot, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(g_reddot, LV_OBJ_FLAG_HIDDEN);
    }
}

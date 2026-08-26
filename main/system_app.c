#include "system_app.h"
#include "esp32_voice.h"
#include "wifi_mgr.h"
#include "ota_client.h"
#include "ui_home.h"
#include "font_mgr.h"

#include "lvgl.h"
#include <string.h>

extern const lv_font_t lv_font_source_han_sans_sc_16_cjk;

static lv_obj_t *g_kb = NULL;
static lv_obj_t *g_wifi_list = NULL;
static lv_timer_t *g_scan_poll_timer = NULL;
static char      g_sel_ssid[33] = "";
static lv_obj_t *g_ver_reddot = NULL;

static void on_back_click(lv_event_t *e)
{
    (void)e;
    /* 此回调本身在 LVGL 任务中执行，直接切回桌面即可。 */
    ui_home_show();
}

static void set_cn_font(lv_obj_t *obj)
{
    const lv_font_t *font = font_mgr_get();
    lv_obj_set_style_text_font(obj,
        font ? font : &lv_font_source_han_sans_sc_16_cjk, 0);
}

/* 带返回栏的新界面；返回按钮回到桌面并刷新红点 */
static lv_obj_t *new_screen(const char *title)
{
    /* 键盘属于旧屏幕；切屏后重新按需创建，避免保留失效对象指针。 */
    g_kb = NULL;
    g_wifi_list = NULL;
    if (g_scan_poll_timer) {
        lv_timer_delete(g_scan_poll_timer);
        g_scan_poll_timer = NULL;
    }
    lv_obj_t *scr = lv_obj_create(NULL);
    set_cn_font(scr);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 12, 0);

    lv_obj_t *top = lv_obj_create(scr);
    lv_obj_set_width(top, LV_PCT(100));
    lv_obj_set_height(top, 44);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_button_create(top);
    lv_obj_set_size(back, 60, 32);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "返回");
    set_cn_font(bl);
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, on_back_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *t = lv_label_create(top);
    lv_label_set_text(t, title);
    set_cn_font(t);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 0);
    return scr;
}

static void keyboard_ready_cb(lv_event_t *e)
{
    (void)e;
    if (g_kb) lv_obj_add_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
}

static void show_keyboard(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    lv_obj_t *scr = lv_screen_active();
    if (!g_kb || lv_obj_get_parent(g_kb) != scr) {
        g_kb = lv_keyboard_create(scr);
        lv_obj_set_width(g_kb, LV_PCT(100));
        lv_obj_align(g_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(g_kb, keyboard_ready_cb, LV_EVENT_READY, NULL);
        lv_obj_add_event_cb(g_kb, keyboard_ready_cb, LV_EVENT_CANCEL, NULL);
    }
    lv_keyboard_set_textarea(g_kb, ta);
    lv_obj_clear_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
}

/* ---------------- 版本信息 ---------------- */
static void on_update_click(lv_event_t *e)
{
    (void)e;
    ui_show_toast("开始更新...");
    ota_client_update(NULL);
}

static void version_screen(void)
{
    lv_obj_t *scr = new_screen("版本信息");

    char ver[32];
    ota_get_running_version(ver, sizeof(ver));
    lv_obj_t *cur = lv_label_create(scr);
    lv_label_set_text_fmt(cur, "当前版本: %s", ver);

    if (g_ota_update_available) {
        lv_obj_t *latest = lv_label_create(scr);
        lv_label_set_text_fmt(latest, "待更新版本: %s", g_ota_latest_version);

        lv_obj_t *desc = lv_label_create(scr);
        lv_label_set_text_fmt(desc, "更新说明:\n%s", g_ota_latest_desc);

        lv_obj_t *upd = lv_button_create(scr);
        lv_obj_set_size(upd, LV_PCT(100), 48);
        lv_obj_t *ul = lv_label_create(upd);
        lv_label_set_text(ul, "更新版本");
        lv_obj_center(ul);
        lv_obj_add_event_cb(upd, on_update_click, LV_EVENT_CLICKED, NULL);
    } else {
        lv_obj_t *latest = lv_label_create(scr);
        lv_label_set_text(latest, "已是最新版本");
    }
    lv_screen_load(scr);
}

/* ---------------- WiFi ---------------- */
static void on_connect_click(lv_event_t *e)
{
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_user_data(e);
    wifi_mgr_connect(g_sel_ssid, lv_textarea_get_text(ta), NULL);
    ui_show_toast("正在连接...");
    if (g_kb) lv_obj_add_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_state(ta, LV_STATE_FOCUSED);
}

static void on_ap_click(lv_event_t *e)
{
    const char *ssid = (const char *)lv_event_get_user_data(e);
    strncpy(g_sel_ssid, ssid, sizeof(g_sel_ssid) - 1);

    lv_obj_t *ta = lv_textarea_create(lv_screen_active());
    lv_textarea_set_placeholder_text(ta, "请输入 WiFi 密码");
    lv_textarea_set_password_mode(ta, true);
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_size(ta, LV_PCT(90), 42);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_add_event_cb(ta, show_keyboard, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ta, show_keyboard, LV_EVENT_FOCUSED, NULL);

    lv_obj_t *conn = lv_button_create(lv_screen_active());
    lv_obj_set_size(conn, LV_PCT(90), 44);
    lv_obj_align(conn, LV_ALIGN_TOP_MID, 0, 108);
    lv_obj_t *cl = lv_label_create(conn);
    lv_label_set_text(cl, "连接");
    lv_obj_center(cl);
    lv_obj_add_event_cb(conn, on_connect_click, LV_EVENT_CLICKED, ta);
}

static void wifi_scan_render(void *data)
{
    (void)data;
    if (!g_wifi_list) return;
    lv_obj_clean(g_wifi_list);
    int n = wifi_mgr_scan_count();
    if (n == 0) {
        lv_obj_t *empty = lv_label_create(g_wifi_list);
        lv_label_set_text(empty, "未找到网络，请重试");
        return;
    }
    for (int i = 0; i < n; i++) {
        const char *ssid = wifi_mgr_scan_ssid(i);
        if (!ssid || !*ssid) continue;
        lv_obj_t *b = lv_button_create(g_wifi_list);
        lv_obj_set_width(b, LV_PCT(100));
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, ssid);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, on_ap_click, LV_EVENT_CLICKED, (void *)ssid);
    }
}

/* WiFi 事件任务绝不触碰 LVGL。结果由 LVGL 定时器轮询并渲染。 */
static void wifi_scan_done(wifi_state_t st, const char *ip)
{
    (void)st;
    (void)ip;
}

static void wifi_scan_poll_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!wifi_mgr_take_scan_result()) return;
    wifi_scan_render(NULL);
    if (g_scan_poll_timer) {
        lv_timer_delete(g_scan_poll_timer);
        g_scan_poll_timer = NULL;
    }
}

static void on_scan_click(lv_event_t *e)
{
    (void)e;
    if (!g_wifi_list || g_scan_poll_timer) return;
    lv_obj_clean(g_wifi_list);
    lv_obj_t *hint = lv_label_create(g_wifi_list);
    lv_label_set_text(hint, "正在扫描网络...");
    set_cn_font(hint);
    wifi_mgr_scan(wifi_scan_done);
    g_scan_poll_timer = lv_timer_create(wifi_scan_poll_cb, 100, NULL);
    ui_show_toast("扫描中...");
}

static void wifi_screen(void)
{
    lv_obj_t *scr = new_screen("WiFi");

    lv_obj_t *scan = lv_button_create(scr);
    lv_obj_set_size(scan, LV_PCT(100), 40);
    lv_obj_t *sl = lv_label_create(scan);
    lv_label_set_text(sl, "扫描网络");
    lv_obj_center(sl);
    lv_obj_add_event_cb(scan, on_scan_click, LV_EVENT_CLICKED, NULL);

    g_wifi_list = lv_obj_create(scr);
    lv_obj_set_flex_grow(g_wifi_list, 1);
    lv_obj_set_flex_flow(g_wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(g_wifi_list, LV_PCT(100));
    lv_obj_set_style_pad_all(g_wifi_list, 6, 0);

    lv_screen_load(scr);
}

/* ---------------- 系统设置主界面 ---------------- */
static void on_wifi_open(lv_event_t *e)  { (void)e; wifi_screen(); }
static void on_ver_open(lv_event_t *e)   { (void)e; version_screen(); }

void system_app_open(void)
{
    lv_obj_t *scr = new_screen("系统设置");

    lv_obj_t *ip = lv_label_create(scr);
    const char *device_ip = wifi_mgr_ip();
    if (wifi_mgr_state() == WIFI_ST_CONNECTED && device_ip && device_ip[0]) {
        lv_label_set_text_fmt(ip, "设备 IP: %s", device_ip);
    } else {
        lv_label_set_text(ip, "设备 IP: 未连接 WiFi");
    }
    set_cn_font(ip);

    lv_obj_t *wifi_btn = lv_button_create(scr);
    lv_obj_set_size(wifi_btn, LV_PCT(100), 50);
    lv_obj_t *wl = lv_label_create(wifi_btn);
    lv_label_set_text(wl, "WiFi");
    lv_obj_center(wl);
    lv_obj_add_event_cb(wifi_btn, on_wifi_open, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ver_btn = lv_button_create(scr);
    lv_obj_set_size(ver_btn, LV_PCT(100), 50);
    lv_obj_t *vl = lv_label_create(ver_btn);
    lv_label_set_text(vl, "版本信息");
    lv_obj_center(vl);
    g_ver_reddot = lv_obj_create(ver_btn);
    lv_obj_set_size(g_ver_reddot, 16, 16);
    lv_obj_set_style_radius(g_ver_reddot, 8, 0);
    lv_obj_set_style_bg_color(g_ver_reddot, lv_color_hex(0xe53e3e), 0);
    lv_obj_set_style_border_width(g_ver_reddot, 0, 0);
    lv_obj_align(g_ver_reddot, LV_ALIGN_TOP_RIGHT, -8, 8);
    if (!g_ota_update_available) lv_obj_add_flag(g_ver_reddot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(ver_btn, on_ver_open, LV_EVENT_CLICKED, NULL);

    lv_screen_load(scr);
}

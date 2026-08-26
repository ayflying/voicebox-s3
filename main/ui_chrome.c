#include "ui_chrome.h"

#include "wifi_mgr.h"
#include "font_mgr.h"

#include <time.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern const lv_font_t lv_font_source_han_sans_sc_16_cjk;

static const char *TAG = "ui_chrome";
static lv_timer_t *s_status_timer = NULL;
static bool s_sntp_started = false;

static void set_cn_font(lv_obj_t *obj)
{
    const lv_font_t *font = font_mgr_get();
    lv_obj_set_style_text_font(obj,
        font ? font : &lv_font_source_han_sans_sc_16_cjk, 0);
}

static void status_refresh_cb(lv_timer_t *timer)
{
    (void)timer;
    time_t now = time(NULL);
    struct tm tm_info = {0};
    char clock_text[8] = "--:--";
    if (now > 1700000000 && localtime_r(&now, &tm_info)) {
        strftime(clock_text, sizeof(clock_text), "%H:%M", &tm_info);
    }

    lv_obj_t *screen = lv_screen_active();
    if (!screen || lv_obj_get_child_count(screen) == 0) return;
    lv_obj_t *bar = lv_obj_get_child(screen, 0);
    if (lv_obj_get_user_data(bar) != (void *)"ui_status") return;
    lv_obj_t *clock = lv_obj_get_child(bar, 0);
    lv_obj_t *wifi = lv_obj_get_child(bar, 1);
    if (clock) lv_label_set_text(clock, clock_text);
    if (wifi) {
        wifi_state_t state = wifi_mgr_state();
        lv_obj_set_style_text_color(wifi,
            state == WIFI_ST_CONNECTED ? lv_color_hex(0x3d7cff) :
            state == WIFI_ST_CONNECTING ? lv_color_hex(0xefaa44) : lv_color_hex(0x9aa5b7), 0);
    }
}

static void sntp_task(void *arg)
{
    (void)arg;
    for (int i = 0; i < 60 && wifi_mgr_state() != WIFI_ST_CONNECTED; ++i) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (wifi_mgr_state() != WIFI_ST_CONNECTED) {
        ESP_LOGW(TAG, "WiFi unavailable; clock remains pending");
        vTaskDelete(NULL);
        return;
    }

    setenv("TZ", "CST-8", 1);
    tzset();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started");
    vTaskDelete(NULL);
}

lv_obj_t *ui_chrome_screen_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    set_cn_font(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_layout(screen, LV_LAYOUT_NONE);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    return screen;
}

void ui_chrome_add_status(lv_obj_t *screen)
{
    lv_obj_t *status = lv_obj_create(screen);
    lv_obj_set_size(status, LV_PCT(100), UI_STATUS_BAR_HEIGHT);
    lv_obj_set_pos(status, 0, 0);
    lv_obj_set_style_bg_color(status, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_width(status, 0, 0);
    lv_obj_set_style_pad_hor(status, 12, 0);
    lv_obj_set_style_pad_ver(status, 0, 0);
    lv_obj_clear_flag(status, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(status, (void *)"ui_status");

    lv_obj_t *clock = lv_label_create(status);
    lv_label_set_text(clock, "--:--");
    lv_obj_set_style_text_font(clock, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(clock, lv_color_hex(0x59657a), 0);
    lv_obj_align(clock, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *wifi = lv_label_create(status);
    lv_label_set_text(wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(wifi, LV_FONT_DEFAULT, 0);
    lv_obj_align(wifi, LV_ALIGN_RIGHT_MID, -47, 0);

    lv_obj_t *battery = lv_label_create(status);
    lv_label_set_text(battery, LV_SYMBOL_BATTERY_EMPTY " --%");
    lv_obj_set_style_text_font(battery, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(battery, lv_color_hex(0x59657a), 0);
    lv_obj_align(battery, LV_ALIGN_RIGHT_MID, 0, 0);

    status_refresh_cb(NULL);
}

void ui_chrome_add_nav(lv_obj_t *screen, const char *title, lv_event_cb_t back_cb)
{
    if (!title && !back_cb) return;
    lv_obj_t *nav = lv_obj_create(screen);
    lv_obj_set_size(nav, LV_PCT(100), UI_NAV_BAR_HEIGHT);
    lv_obj_set_pos(nav, 0, UI_STATUS_BAR_HEIGHT);
    lv_obj_set_style_bg_color(nav, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_width(nav, 0, 0);
    lv_obj_set_style_pad_hor(nav, 12, 0);
    lv_obj_set_style_pad_ver(nav, 0, 0);
    lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);

    if (back_cb) {
        lv_obj_t *back = lv_button_create(nav);
        lv_obj_set_size(back, 32, 28);
        lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(back, lv_color_hex(0xf0f3f8), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(back, 0, 0);
        lv_obj_set_style_shadow_width(back, 0, 0);
        lv_obj_set_style_pad_all(back, 0, 0);
        lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *icon = lv_label_create(back);
        lv_label_set_text(icon, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_font(icon, LV_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(icon, lv_color_hex(0x526078), 0);
        lv_obj_add_flag(icon, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_center(icon);
    }

    if (title) {
        lv_obj_t *label = lv_label_create(nav);
        lv_label_set_text(label, title);
        set_cn_font(label);
        lv_obj_set_style_text_color(label, lv_color_hex(0x172033), 0);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    }
}

lv_obj_t *ui_chrome_add_content(lv_obj_t *screen, uint16_t pad_hor, uint16_t pad_ver)
{
    lv_obj_t *content = lv_obj_create(screen);
    lv_obj_set_size(content, LV_PCT(100), 320 - UI_CONTENT_TOP);
    lv_obj_set_pos(content, 0, UI_CONTENT_TOP);
    lv_obj_set_style_bg_color(content, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_hor(content, pad_hor, 0);
    lv_obj_set_style_pad_ver(content, pad_ver, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    return content;
}

void ui_chrome_time_sync_start(void)
{
    if (!s_status_timer) s_status_timer = lv_timer_create(status_refresh_cb, 1000, NULL);
    if (!s_sntp_started) {
        s_sntp_started = true;
        xTaskCreate(sntp_task, "sntp_sync", 3072, NULL, 3, NULL);
    }
}

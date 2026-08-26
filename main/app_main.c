#include "esp32_voice.h"
#include "wifi_mgr.h"
#include "tts.h"
#include "ota_client.h"
#include "ui_home.h"
#include "voice_app.h"
#include "font_mgr.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "bsp/esp32_s3_es3c28p.h"
#include "lvgl.h"

static const char *TAG = "app_main";

/* 轻量提示条（LVGL，1.5s 后自动消失） */
static void toast_timer_cb(lv_timer_t *t)
{
    lv_obj_t *obj = (lv_obj_t *)lv_timer_get_user_data(t);
    lv_obj_del(obj);
    lv_timer_del(t);
}

void ui_show_toast(const char *msg)
{
    lv_obj_t *t = lv_label_create(lv_layer_top());
    lv_label_set_text(t, msg);
    const lv_font_t *font = font_mgr_get();
    if (font) lv_obj_set_style_text_font(t, font, 0);
    lv_obj_align(t, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_color(t, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(t, 8, 0);
    lv_obj_set_style_radius(t, 6, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_timer_create(toast_timer_cb, 1500, t);
}

/* 启动后静默查 OTA 版本；等 WiFi 连上再查，结果用异步刷新红点 */
/* 临时实机自检：不操作 UI，仅验证 WiFi 扫描完整生命周期与结果读取。 */
static void wifi_scan_selftest_done(wifi_state_t state, const char *ip)
{
    (void)state;
    (void)ip;
    ESP_LOGI(TAG, "wifi scan self-test callback: %d AP(s)", wifi_mgr_scan_count());
}

static void wifi_scan_selftest_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(3000));
    ESP_LOGI(TAG, "wifi scan self-test: starting");
    wifi_mgr_scan(wifi_scan_selftest_done);
    vTaskDelete(NULL);
}

static void ota_check_task(void *arg)
{
    (void)arg;
    /* 等 WiFi 连接（最长 ~30s） */
    for (int i = 0; i < 60; i++) {
        if (wifi_mgr_state() == WIFI_ST_CONNECTED) break;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ota_client_check();
    lv_async_call((lv_async_cb_t)ui_home_refresh, NULL);
    vTaskDelete(NULL);
}

void app_main(void)
{
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* BOX-3 BSP v3.x：一体化初始化 LVGL、LCD 与触摸。 */
    lv_display_t *disp = bsp_display_start();
    ESP_ERROR_CHECK(disp ? ESP_OK : ESP_FAIL);
    bsp_display_brightness_set(100);
    bsp_display_backlight_on();

    /* BSP v3.x 使用 esp_codec_dev 句柄；播放与录音分别初始化。 */
    ESP_ERROR_CHECK(bsp_audio_init(NULL));
    esp_codec_dev_handle_t speaker = bsp_audio_codec_speaker_init();
    esp_codec_dev_handle_t microphone = bsp_audio_codec_microphone_init();
    ESP_ERROR_CHECK(speaker ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(microphone ? ESP_OK : ESP_FAIL);
    tts_set_codec(speaker);
    voice_app_set_codec(microphone);
    tts_init();

    /* WiFi（若有保存则自动重连） */
    wifi_mgr_init();

    /* 外置 UTF-8 中文字库在 font_data 分区；加载失败自动降级内置字库。 */
    font_mgr_init();

    /* 桌面：LVGL 由独立任务驱动，应用任务创建控件前必须持有 BSP 锁。 */
    if (bsp_display_lock(0)) {
        ui_home_show();
        bsp_display_unlock();
    } else {
        ESP_LOGE(TAG, "failed to acquire LVGL lock for home screen");
    }

    /* 启动后执行一次不触碰 UI 的 WiFi 扫描实机自检。 */
    xTaskCreate(wifi_scan_selftest_task, "wifi_scan_test", 4096, NULL, 3, NULL);

    /* 后台查 OTA 版本（驱动红点） */
    xTaskCreate(ota_check_task, "ota_check", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "app started, version %s", APP_VERSION);
}

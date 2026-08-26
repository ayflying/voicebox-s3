#include "voice_app.h"
#include "esp32_voice.h"
#include "tts.h"
#include "ui_home.h"
#include "font_mgr.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "lvgl.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "esp_codec_dev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

static const char *TAG = "voice_app";

static esp_codec_dev_handle_t s_codec = NULL;
static lv_obj_t *g_text_label = NULL;
static lv_obj_t *g_status_label = NULL;

extern const lv_font_t lv_font_source_han_sans_sc_16_cjk;

static void set_utf8_font(lv_obj_t *obj)
{
    const lv_font_t *font = font_mgr_get();
    lv_obj_set_style_text_font(obj,
        font ? font : &lv_font_source_han_sans_sc_16_cjk, 0);
}

/* 录音缓冲（PSRAM） */
static uint8_t *s_rec_buf = NULL;
static size_t   s_rec_len = 0;
static size_t   s_rec_cap = 0;
static bool     s_recording = false;
static TaskHandle_t s_rec_task = NULL;

void voice_app_set_codec(void *codec_handle)
{
    s_codec = (esp_codec_dev_handle_t)codec_handle;
}

static bool rec_buf_append(const uint8_t *data, size_t len)
{
    if (s_rec_len + len > s_rec_cap) {
        size_t newcap = s_rec_cap ? s_rec_cap * 2 : 32768;
        while (newcap < s_rec_len + len) newcap *= 2;
        uint8_t *nb = heap_caps_realloc(s_rec_buf, newcap, MALLOC_CAP_SPIRAM);
        if (!nb) return false;
        s_rec_buf = nb;
        s_rec_cap = newcap;
    }
    memcpy(s_rec_buf + s_rec_len, data, len);
    s_rec_len += len;
    return true;
}

static void rec_task(void *arg)
{
    (void)arg;
    uint8_t tmp[1024];
    while (s_recording) {
        int r = esp_codec_dev_read(s_codec, tmp, sizeof(tmp));
        if (r > 0) rec_buf_append(tmp, r);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    vTaskDelete(NULL);
}

/* 把录音 POST 到网关 /transcribe，返回识别文本（调用方 free） */
static char *transcribe(uint8_t *pcm, size_t len)
{
    char url[128], token[128];
    net_cfg_get_url(url, sizeof(url));
    net_cfg_get_token(token, sizeof(token));
    char post_url[160];
    snprintf(post_url, sizeof(post_url), "%s/transcribe", url);

    esp_http_client_config_t cfg = {
        .url = post_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 20000,
        .buffer_size = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "X-Auth-Token", token);
    esp_http_client_set_header(c, "Content-Type", "application/octet-stream");
    esp_http_client_open(c, len);
    int w = esp_http_client_write(c, (const char *)pcm, len);
    if (w < 0) {
        ESP_LOGE(TAG, "http write failed");
        esp_http_client_cleanup(c);
        return NULL;
    }
    esp_http_client_fetch_headers(c);
    int cl = esp_http_client_get_content_length(c);
    if (cl <= 0) cl = 4096;
    char *body = malloc(cl + 1);
    int total = 0, r;
    while ((r = esp_http_client_read(c, body + total, cl - total)) > 0) {
        total += r;
        if (total >= cl) break;
    }
    body[total] = '\0';
    int code = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);

    if (code != 200) {
        ESP_LOGW(TAG, "transcribe http %d", code);
        free(body);
        return NULL;
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return NULL;
    cJSON *jt = cJSON_GetObjectItem(root, "text");
    char *txt = (jt && jt->valuestring) ? strdup(jt->valuestring) : NULL;
    cJSON_Delete(root);
    return txt;
}

static void speak_task(void *arg)
{
    char *txt = (char *)arg;
    vTaskDelay(pdMS_TO_TICKS(5000));   /* 显示后等 5 秒 */
    if (txt && *txt) {
        if (g_status_label) lv_label_set_text(g_status_label, "朗读中...");
        tts_speak(txt);
    }
    free(txt);
    if (g_status_label) lv_label_set_text(g_status_label, "按住说话");
    vTaskDelete(NULL);
}

static void on_pressed(lv_event_t *e)
{
    (void)e;
    if (!s_codec) { ui_show_toast("音频未就绪"); return; }
    s_rec_len = 0;
    s_recording = true;
    lv_label_set_text(g_status_label, "聆听中...");
    xTaskCreate(rec_task, "rec", 4096, NULL, 5, &s_rec_task);
}

static void on_released(lv_event_t *e)
{
    (void)e;
    if (!s_recording) return;
    s_recording = false;
    lv_label_set_text(g_status_label, "识别中...");

    /* 录音任务可能尚未写入首帧，空缓冲时不能解引用 s_rec_buf。 */
    size_t n = s_rec_len;
    if (n == 0 || s_rec_buf == NULL) {
        lv_label_set_text(g_text_label, "录音时间太短");
        lv_label_set_text(g_status_label, "按住说话");
        return;
    }

    /* 拷贝录音缓冲（避免被后续覆盖）。 */
    uint8_t *pcm = malloc(n);
    if (pcm == NULL) {
        ESP_LOGE(TAG, "alloc %u bytes for recording failed", (unsigned)n);
        lv_label_set_text(g_text_label, "内存不足，请重试");
        lv_label_set_text(g_status_label, "按住说话");
        return;
    }
    memcpy(pcm, s_rec_buf, n);

    char *txt = (n > 1000) ? transcribe(pcm, n) : NULL;  /* 太短忽略 */
    free(pcm);

    if (txt && *txt) {
        lv_label_set_text(g_text_label, txt);
        /* 5 秒后朗读 */
        char *dup = strdup(txt);
        xTaskCreate(speak_task, "speak", 4096, dup, 5, NULL);
    } else {
        lv_label_set_text(g_text_label, txt ? txt : "(未识别到语音)");
        lv_label_set_text(g_status_label, "按住说话");
    }
    free(txt);
}

void voice_app_open(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    set_utf8_font(scr);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 16, 0);

    lv_obj_t *top = lv_obj_create(scr);
    lv_obj_set_width(top, LV_PCT(100));
    lv_obj_set_height(top, 44);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *back = lv_button_create(top);
    lv_obj_set_size(back, 60, 32);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "返回");
    set_utf8_font(bl);
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, (lv_event_cb_t)ui_home_show, LV_EVENT_CLICKED, NULL);

    g_text_label = lv_label_create(scr);
    lv_label_set_text(g_text_label, "（识别结果会显示在这里）");
    set_utf8_font(g_text_label);
    lv_obj_set_flex_grow(g_text_label, 1);
    lv_obj_set_style_text_align(g_text_label, LV_TEXT_ALIGN_CENTER, 0);

    g_status_label = lv_label_create(scr);
    lv_label_set_text(g_status_label, "按住说话");
    set_utf8_font(g_status_label);
    lv_obj_align(g_status_label, LV_ALIGN_BOTTOM_MID, 0, -90);

    lv_obj_t *mic = lv_button_create(scr);
    lv_obj_set_size(mic, 160, 160);
    lv_obj_align(mic, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_radius(mic, 80, 0);
    lv_obj_set_style_bg_color(mic, lv_color_hex(0x38a169), 0);
    lv_obj_t *ml = lv_label_create(mic);
    lv_label_set_text(ml, "按住");
    set_utf8_font(ml);
    lv_obj_center(ml);
    lv_obj_add_event_cb(mic, on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(mic, on_released, LV_EVENT_RELEASED, NULL);

    lv_screen_load(scr);
}

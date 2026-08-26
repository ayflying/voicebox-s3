#include "tts.h"
#include "esp32_voice.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "esp_tts.h"
#include "esp_tts_voice_xiaole.h"
#include "esp_codec_dev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

static const char *TAG = "tts";

/* voice_data 分区子类型（与 partitions.csv 一致） */
#define VOICE_PART_SUBTYPE 0x99

static esp_tts_handle_t s_tts_handle = NULL;
static esp_tts_voice_t *s_voice = NULL;
static esp_codec_dev_handle_t s_codec = NULL;
static bool              s_ready = false;

void tts_set_codec(void *codec_handle)
{
    s_codec = (esp_codec_dev_handle_t)codec_handle;
}

void tts_init(void)
{
    if (s_ready) return;

    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, VOICE_PART_SUBTYPE, "voice_data");
    if (!part) {
        ESP_LOGE(TAG, "voice_data partition not found! flash the TTS model first.");
        return;
    }

    /* 模型较大，放 PSRAM */
    void *part_data = heap_caps_malloc(part->size, MALLOC_CAP_SPIRAM);
    if (!part_data) {
        ESP_LOGE(TAG, "cannot alloc %d bytes for TTS model", part->size);
        return;
    }
    if (esp_partition_read(part, 0, part_data, part->size) != ESP_OK) {
        ESP_LOGE(TAG, "read voice_data failed");
        free(part_data);
        return;
    }

    s_voice = esp_tts_voice_set_init(&esp_tts_voice_xiaole, part_data);
    if (!s_voice) {
        ESP_LOGE(TAG, "esp_tts_voice_set_init failed");
        free(part_data);
        return;
    }
    s_tts_handle = esp_tts_create(s_voice);
    if (!s_tts_handle) {
        ESP_LOGE(TAG, "esp_tts_create failed");
        return;
    }
    s_ready = true;
    ESP_LOGI(TAG, "TTS ready (model %d bytes)", part->size);
}

bool tts_is_ready(void) { return s_ready; }

bool tts_speak(const char *text)
{
    if (!s_ready || !s_codec || !text || !*text) return false;

    int total = esp_tts_parse_chinese(s_tts_handle, text);
    if (total <= 0) {
        ESP_LOGW(TAG, "parse returned %d", total);
        return false;
    }

    int pcm_len = 0;
    short *pcm = NULL;
    while ((pcm = esp_tts_stream_play(s_tts_handle, &pcm_len, 3)) != NULL) {
        if (pcm_len <= 0) break;
        esp_codec_dev_write(s_codec, pcm, pcm_len * (int)sizeof(short));
        /* 近似实时：写入后让出少量时间，避免缓冲溢出 */
        vTaskDelay(pdMS_TO_TICKS((pcm_len / 2) * 1000 / 16000));
    }
    ESP_LOGI(TAG, "TTS spoke: %s", text);
    return true;
}

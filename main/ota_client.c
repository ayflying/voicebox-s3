#include "ota_client.h"
#include "esp32_voice.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "mbedtls/md5.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ota_client";

/* 全局 OTA 状态（供桌面/版本信息红点读取） */
volatile bool g_ota_update_available = false;
char g_ota_latest_version[32] = "0.0.0";
char g_ota_latest_desc[256] = "";
char g_ota_latest_md5[40] = "";
int  g_ota_latest_size = 0;

/* ---------------- NVS 网络配置 ---------------- */

static void nvs_get(const char *ns, const char *key, char *out, size_t len)
{
    nvs_handle_t h;
    out[0] = '\0';
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return;
    size_t rl = len;
    if (nvs_get_str(h, key, out, &rl) != ESP_OK) out[0] = '\0';
    nvs_close(h);
}

static void nvs_set(const char *ns, const char *key, const char *val)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, key, val);
    nvs_commit(h);
    nvs_close(h);
}

void net_cfg_get_url(char *buf, size_t len)
{
    if (!buf || len == 0) return;
    /* 网关域名由固件固定，忽略历史 NVS 中的可编辑地址。 */
    strlcpy(buf, DEFAULT_GATEWAY_URL, len);
}

void net_cfg_get_token(char *buf, size_t len)
{
    nvs_get(NVS_NS_NET, NVS_KEY_TOKEN, buf, len);
    if (buf[0] == '\0') strncpy(buf, DEFAULT_AUTH_TOKEN, len - 1);
}

void net_cfg_set(const char *url, const char *token)
{
    (void)url; /* 网关地址固定为 DEFAULT_GATEWAY_URL。 */
    if (token && token[0]) nvs_set(NVS_NS_NET, NVS_KEY_TOKEN, token);
}

/* ---------------- 版本比较 ---------------- */

static int ver_cmp(const char *a, const char *b)
{
    int pa[3] = {0, 0, 0}, pb[3] = {0, 0, 0};
    sscanf(a, "%d.%d.%d", &pa[0], &pa[1], &pa[2]);
    sscanf(b, "%d.%d.%d", &pb[0], &pb[1], &pb[2]);
    for (int i = 0; i < 3; i++) {
        if (pa[i] != pb[i]) return pa[i] - pb[i];
    }
    return 0;
}

static void get_running_version(char *buf, size_t len)
{
    nvs_get(NVS_NS_OTA, NVS_KEY_FW_VER, buf, len);
    if (buf[0] == '\0') strncpy(buf, APP_VERSION, len - 1);
}

void ota_get_running_version(char *buf, size_t len)
{
    get_running_version(buf, len);
}

/* ---------------- HTTP 简单 GET（取文本） ---------------- */

static char *http_get_text(const char *url, const char *token, int *http_code)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
        .buffer_size = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "X-Auth-Token", token);
    esp_err_t err = esp_http_client_open(c, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "http open failed: %s", esp_err_to_name(err));
        if (http_code) *http_code = 0;
        esp_http_client_cleanup(c);
        return NULL;
    }
    esp_http_client_fetch_headers(c);
    if (http_code) *http_code = esp_http_client_get_status_code(c);
    int len = esp_http_client_get_content_length(c);
    if (len <= 0) len = 4096;
    char *buf = malloc(len + 1);
    int total = 0;
    int r;
    while ((r = esp_http_client_read(c, buf + total, len - total)) > 0) {
        total += r;
        if (total >= len) break;
    }
    buf[total] = '\0';
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return buf;
}

/* ---------------- 自查版本 ---------------- */

bool ota_client_check(void)
{
    char url[128], token[128], ver[32];
    net_cfg_get_url(url, sizeof(url));
    net_cfg_get_token(token, sizeof(token));
    get_running_version(ver, sizeof(ver));

    char req[192];
    snprintf(req, sizeof(req), "%s%s?current=%s", url, OTA_VERSION_PATH, ver);

    int code = 0;
    char *body = http_get_text(req, token, &code);
    if (!body || code != 200) {
        ESP_LOGW(TAG, "version check failed code=%d", code);
        free(body);
        g_ota_update_available = false;
        return false;
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        g_ota_update_available = false;
        return false;
    }

    bool upToDate = cJSON_GetObjectItem(root, "upToDate") &&
                    cJSON_GetObjectItem(root, "upToDate")->valueint;
    if (upToDate) {
        g_ota_update_available = false;
        cJSON_Delete(root);
        return false;
    }

    cJSON *jv = cJSON_GetObjectItem(root, "version");
    cJSON *jm = cJSON_GetObjectItem(root, "md5");
    cJSON *js = cJSON_GetObjectItem(root, "size");
    cJSON *jd = cJSON_GetObjectItem(root, "description");
    if (jv && jv->valuestring) {
        strncpy(g_ota_latest_version, jv->valuestring, sizeof(g_ota_latest_version) - 1);
        if (jm && jm->valuestring)
            strncpy(g_ota_latest_md5, jm->valuestring, sizeof(g_ota_latest_md5) - 1);
        if (js) g_ota_latest_size = js->valueint;
        if (jd && jd->valuestring)
            strncpy(g_ota_latest_desc, jd->valuestring, sizeof(g_ota_latest_desc) - 1);
        g_ota_update_available = (ver_cmp(g_ota_latest_version, ver) > 0);
    } else {
        g_ota_update_available = false;
    }
    cJSON_Delete(root);
    ESP_LOGI(TAG, "check: local=%s remote=%s update=%d", ver, g_ota_latest_version, g_ota_update_available);
    return g_ota_update_available;
}

/* ---------------- 执行更新 ---------------- */

bool ota_client_update(ota_progress_cb_t cb)
{
    if (!g_ota_update_available) return false;

    char url[128], token[128];
    net_cfg_get_url(url, sizeof(url));
    net_cfg_get_token(token, sizeof(token));
    char fw_url[192];
    snprintf(fw_url, sizeof(fw_url), "%s%s", url, OTA_FIRMWARE_PATH);

    esp_http_client_config_t cfg = {
        .url = fw_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 120000,
        .buffer_size = 8192,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "X-Auth-Token", token);
    if (esp_http_client_open(c, 0) != ESP_OK) {
        ESP_LOGE(TAG, "open firmware failed");
        esp_http_client_cleanup(c);
        return false;
    }
    int code = esp_http_client_fetch_headers(c);
    if (code != 200) {
        ESP_LOGE(TAG, "firmware http %d", code);
        esp_http_client_cleanup(c);
        return false;
    }
    char *expect_md5 = NULL;
    if (esp_http_client_get_header(c, "X-MD5", &expect_md5) != ESP_OK || !expect_md5) {
        ESP_LOGE(TAG, "firmware response missing X-MD5");
        esp_http_client_cleanup(c);
        return false;
    }
    int content_len = esp_http_client_get_content_length(c);

    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    if (!update) {
        ESP_LOGE(TAG, "no update partition");
        esp_http_client_cleanup(c);
        return false;
    }
    esp_ota_handle_t ota_handle;
    if (esp_ota_begin(update, OTA_SIZE_UNKNOWN, &ota_handle) != ESP_OK) {
        ESP_LOGE(TAG, "ota_begin failed");
        esp_http_client_cleanup(c);
        return false;
    }

    mbedtls_md5_context md5_ctx;
    mbedtls_md5_init(&md5_ctx);
    mbedtls_md5_starts(&md5_ctx);

    uint8_t buf[8192];
    int total = 0, r;
    while ((r = esp_http_client_read(c, (char *)buf, sizeof(buf))) > 0) {
        esp_ota_write(ota_handle, buf, r);
        mbedtls_md5_update(&md5_ctx, buf, r);
        total += r;
        if (cb && content_len > 0)
            cb(total * 100 / content_len, "下载中");
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);

    unsigned char md5_out[16];
    mbedtls_md5_finish(&md5_ctx, md5_out);
    mbedtls_md5_free(&md5_ctx);

    char got_md5[33];
    for (int i = 0; i < 16; i++) sprintf(got_md5 + i * 2, "%02x", md5_out[i]);

    ESP_LOGI(TAG, "downloaded=%d expect_md5=%s got_md5=%s", total, expect_md5 ? expect_md5 : "?", got_md5);

    if (expect_md5 && strcmp(got_md5, expect_md5) != 0) {
        ESP_LOGE(TAG, "MD5 mismatch! abort update.");
        esp_ota_end(ota_handle);   // 结束但不置位 boot，保留旧槽
        if (cb) cb(0, "校验失败：文件损坏，已取消更新");
        return false;
    }

    if (esp_ota_end(ota_handle) != ESP_OK) {
        ESP_LOGE(TAG, "ota_end failed");
        if (cb) cb(0, "写入失败");
        return false;
    }
    if (esp_ota_set_boot_partition(update) != ESP_OK) {
        ESP_LOGE(TAG, "set_boot failed");
        if (cb) cb(0, "设置启动分区失败");
        return false;
    }

    /* 记录新版本号到 NVS（启动后用于比对） */
    nvs_set(NVS_NS_OTA, NVS_KEY_FW_VER, g_ota_latest_version);

    if (cb) cb(100, "更新完成，即将重启");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return true; /* 不会到达 */
}

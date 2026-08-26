#include "wifi_mgr.h"
#include "esp32_voice.h"

#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "wifi_mgr";

#define MAX_SCAN 32
#define SSID_MAX 33

static wifi_state_t    g_state = WIFI_ST_DISCONNECTED;
static char            g_ip[16] = "";
static wifi_state_cb_t g_scan_cb = NULL;
static wifi_state_cb_t g_conn_cb = NULL;

static char             g_scan_ssid[MAX_SCAN][SSID_MAX];
static wifi_ap_record_t g_scan_records[MAX_SCAN]; /* 避免占用 WiFi 事件任务栈 */
static int              g_scan_cnt = 0;
static bool             g_scan_in_progress = false;
static volatile bool    g_scan_result_ready = false;

static void nvs_read_str(const char *ns, const char *key, char *out, size_t len)
{
    nvs_handle_t h;
    out[0] = '\0';
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return;
    size_t rl = len;
    if (nvs_get_str(h, key, out, &rl) != ESP_OK) out[0] = '\0';
    nvs_close(h);
}

static void nvs_write_str(const char *ns, const char *key, const char *val)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, key, val);
    nvs_commit(h);
    nvs_close(h);
}

static void notify(wifi_state_cb_t cb, wifi_state_t st)
{
    if (cb) cb(st, g_ip);
}

static void event_handler(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        g_state = WIFI_ST_DISCONNECTED;
        ESP_LOGW(TAG, "sta disconnected, retry");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        snprintf(g_ip, sizeof(g_ip), IPSTR, IP2STR(&e->ip_info.ip));
        g_state = WIFI_ST_CONNECTED;
        ESP_LOGI(TAG, "got ip %s", g_ip);
        notify(g_conn_cb, WIFI_ST_CONNECTED);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        /* ESP event task 栈较小；扫描记录放静态内存，避免局部数组导致栈溢出重启。 */
        uint16_t n = MAX_SCAN;
        esp_err_t err = esp_wifi_scan_get_ap_records(&n, g_scan_records);
        g_scan_cnt = 0;
        if (err == ESP_OK) {
            for (int i = 0; i < n && g_scan_cnt < MAX_SCAN; i++) {
                if (g_scan_records[i].ssid[0] == '\0') continue;
                strlcpy(g_scan_ssid[g_scan_cnt], (char *)g_scan_records[i].ssid, SSID_MAX);
                g_scan_cnt++;
            }
            ESP_LOGI(TAG, "scan done: %d aps", g_scan_cnt);
        } else {
            ESP_LOGE(TAG, "read scan records failed: %s", esp_err_to_name(err));
        }
        g_scan_in_progress = false;
        g_scan_result_ready = true;
        notify(g_scan_cb, g_state);
        g_scan_cb = NULL;
    }
}

void wifi_mgr_init(void)
{
    static bool inited = false;
    if (inited) return;
    inited = true;

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    char ssid[SSID_MAX] = "", pass[65] = "";
    wifi_mgr_get_saved(ssid, sizeof(ssid), pass, sizeof(pass));
    if (strlen(ssid) > 0) {
        g_state = WIFI_ST_CONNECTING;
        wifi_config_t wcfg = {0};
        strncpy((char *)wcfg.sta.ssid, ssid, 32);
        strncpy((char *)wcfg.sta.password, pass, 64);
        esp_wifi_set_config(WIFI_IF_STA, &wcfg);
        esp_wifi_connect();
    }
}

wifi_state_t wifi_mgr_state(void) { return g_state; }
const char *wifi_mgr_ip(void)     { return g_ip; }

void wifi_mgr_scan(wifi_state_cb_t cb)
{
    if (g_scan_in_progress) {
        ESP_LOGW(TAG, "scan already in progress");
        return;
    }
    g_scan_cb = cb;
    g_scan_cnt = 0;
    g_scan_result_ready = false;
    g_scan_in_progress = true; /* 在启动调用前置位，避免极快完成时状态错乱。 */
    wifi_scan_config_t sc = {0};
    sc.show_hidden = true;
    esp_err_t err = esp_wifi_scan_start(&sc, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start scan failed: %s", esp_err_to_name(err));
        g_scan_in_progress = false;
        notify(g_scan_cb, g_state);
        g_scan_cb = NULL;
    }
}

bool wifi_mgr_take_scan_result(void)
{
    if (!g_scan_result_ready) return false;
    g_scan_result_ready = false;
    return true;
}

int wifi_mgr_scan_count(void) { return g_scan_cnt; }
const char *wifi_mgr_scan_ssid(int i)
{
    return (i >= 0 && i < g_scan_cnt) ? g_scan_ssid[i] : NULL;
}

void wifi_mgr_connect(const char *ssid, const char *pass, wifi_state_cb_t cb)
{
    g_conn_cb = cb;
    g_state = WIFI_ST_CONNECTING;
    wifi_config_t wcfg = {0};
    strncpy((char *)wcfg.sta.ssid, ssid, 32);
    strncpy((char *)wcfg.sta.password, pass, 64);
    esp_wifi_set_config(WIFI_IF_STA, &wcfg);
    nvs_write_str(NVS_NS_WIFI, NVS_KEY_SSID, ssid);
    nvs_write_str(NVS_NS_WIFI, NVS_KEY_PASS, pass);
    esp_wifi_connect();
}

bool wifi_mgr_has_saved(void)
{
    char ssid[SSID_MAX] = "";
    wifi_mgr_get_saved(ssid, sizeof(ssid), NULL, 0);
    return strlen(ssid) > 0;
}

void wifi_mgr_get_saved(char *ssid_buf, size_t ssid_len,
                        char *pass_buf, size_t pass_len)
{
    nvs_read_str(NVS_NS_WIFI, NVS_KEY_SSID, ssid_buf, ssid_len);
    if (pass_buf) nvs_read_str(NVS_NS_WIFI, NVS_KEY_PASS, pass_buf, pass_len);
}

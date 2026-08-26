#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    WIFI_ST_DISCONNECTED = 0,
    WIFI_ST_CONNECTING,
    WIFI_ST_CONNECTED,
} wifi_state_t;

typedef void (*wifi_state_cb_t)(wifi_state_t state, const char *ip);

/* 初始化 WiFi（station），若 NVS 有保存则自动重连 */
void wifi_mgr_init(void);

wifi_state_t wifi_mgr_state(void);
const char  *wifi_mgr_ip(void);

/* 扫描附近 AP；完成（或失败）后调用 cb。结果用下面两个函数读取 */
void wifi_mgr_scan(wifi_state_cb_t cb);
bool wifi_mgr_take_scan_result(void);
int  wifi_mgr_scan_count(void);
const char *wifi_mgr_scan_ssid(int i);

/* 连接指定 SSID/密码，成功后写入 NVS；结果通过 cb 通知 */
void wifi_mgr_connect(const char *ssid, const char *pass, wifi_state_cb_t cb);

/* 读取已保存的凭据 */
bool wifi_mgr_has_saved(void);
void wifi_mgr_get_saved(char *ssid_buf, size_t ssid_len,
                        char *pass_buf, size_t pass_len);

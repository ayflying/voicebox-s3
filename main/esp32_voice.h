#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* 当前固件版本（OTA 校验/红点比对用） */
#define APP_VERSION        "1.0.1"

/* NVS 命名空间与键 */
#define NVS_NS_WIFI        "wifi"
#define NVS_NS_NET         "net"
#define NVS_NS_OTA         "ota"
#define NVS_KEY_SSID       "ssid"
#define NVS_KEY_PASS       "pass"
#define NVS_KEY_GW_URL     "gw_url"
#define NVS_KEY_TOKEN      "token"
#define NVS_KEY_FW_VER     "fw_ver"

/* 固定公网网关：不从 NVS 读取，也不向用户提供修改入口。
 * OTA 分发接口当前以免认证模式调试；ASR 鉴权后续单独设计。 */
#define DEFAULT_GATEWAY_URL "https://esp.luoe.cn"
/* 仅兼容现有 ASR 调用；OTA 请求不再读取或发送此值。 */
#define DEFAULT_AUTH_TOKEN  "fb6435a2346f5ec0fd36b660b81b513eda56ddb0833829173636fd283dad2827"

/* OTA 分发接口路径（挂在网关 18080 下） */
#define OTA_VERSION_PATH   "/api/ota/version"
#define OTA_FIRMWARE_PATH  "/api/ota/firmware"

/* 全局 OTA 状态（启动自查后置位，驱动红点） */
extern volatile bool g_ota_update_available;
extern char          g_ota_latest_version[32];
extern char          g_ota_latest_desc[256];
extern char          g_ota_latest_md5[40];
extern int           g_ota_latest_size;

/* 轻量提示条（LVGL，各 app 可调用） */
void ui_show_toast(const char *msg);

/* 进入各 app 的入口（在 ui_home 注册） */
void voice_app_open(void);
void system_app_open(void);

/* 固定网关地址、ASR 兼容令牌及芯片 eFuse 中的原始 128-bit UUID。 */
void net_cfg_get_url(char *buf, size_t len);
void net_cfg_get_token(char *buf, size_t len);
void net_cfg_set(const char *url, const char *token);
void device_uuid_get(char *buf, size_t len);

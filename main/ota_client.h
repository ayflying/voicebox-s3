#pragma once

#include <stdbool.h>
#include <stddef.h>

/* OTA 进度回调：percent 0~100，status 为阶段描述 */
typedef void (*ota_progress_cb_t)(int percent, const char *status);

/* 启动后自查版本：拉取网关 /api/ota/version，与本地版本比较；
   若服务端更新则置位 g_ota_update_available 并填充版本信息全局变量。
   返回 true 表示「有待更新版本」。 */
bool ota_client_check(void);

/* 获取当前实际运行版本（优先 NVS 中记录，否则编译期 APP_VERSION） */
void ota_get_running_version(char *buf, size_t len);

/* 执行更新：下载固件 -> 增量算 MD5 -> 与响应头 X-MD5 比对；
   不一致立即中止（不写 boot 分区，提示校验失败）；一致则写 OTA 槽并重启。
   cb 可用于刷新进度条。成功写入后会直接 esp_restart()。 */
bool ota_client_update(ota_progress_cb_t cb);

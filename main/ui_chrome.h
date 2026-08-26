#pragma once

#include "lvgl.h"

/* 统一 240×320 页面框架：顶部状态栏 20px、导航栏 34px、内容从 y=54 开始。 */
#define UI_STATUS_BAR_HEIGHT 20
#define UI_NAV_BAR_HEIGHT    34
#define UI_CONTENT_TOP       (UI_STATUS_BAR_HEIGHT + UI_NAV_BAR_HEIGHT)

/* 创建无布局的白色全屏页面；调用方负责创建内容区。 */
lv_obj_t *ui_chrome_screen_create(void);

/* 在所有页面最上方创建统一状态栏：时间、WiFi、电量。 */
void ui_chrome_add_status(lv_obj_t *screen);

/* 创建统一导航栏。title 为 NULL 时只保留状态栏；back_cb 为 NULL 时不显示返回按钮。 */
void ui_chrome_add_nav(lv_obj_t *screen, const char *title, lv_event_cb_t back_cb);

/* 创建位于通用顶部栏下方的内容区，高度为 320 - UI_CONTENT_TOP。 */
lv_obj_t *ui_chrome_add_content(lv_obj_t *screen, uint16_t pad_hor, uint16_t pad_ver);

/* 启动网络授时；联网后自动同步中国标准时间并刷新所有页面顶部时间。 */
void ui_chrome_time_sync_start(void);

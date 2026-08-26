#pragma once

#include "lvgl.h"

/* 从独立 font_data 分区加载 UTF-8 中文 LVGL 二进制字库。 */
void font_mgr_init(void);
const lv_font_t *font_mgr_get(void);
bool font_mgr_is_external_ready(void);

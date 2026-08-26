#include "font_mgr.h"

#include "esp_log.h"

extern const lv_font_t lv_font_utf8_zh_16;

void font_mgr_init(void)
{
    /* UTF-8 字形作为编译期资源使用，避免 LVGL 9.5 二进制字库加载器缺陷。 */
    ESP_LOGI("font", "UTF-8 Chinese font ready (CJK U+4E00-U+9FA5)");
}

const lv_font_t *font_mgr_get(void)
{
    return &lv_font_utf8_zh_16;
}

bool font_mgr_is_external_ready(void)
{
    return true;
}

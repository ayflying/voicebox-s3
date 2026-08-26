#pragma once

#include <stdbool.h>

/* 初始化本地离线 TTS（从 voice_data 分区加载中文语音库） */
void tts_init(void);
bool tts_is_ready(void);

/* 设置播放用的音频编解码句柄（BSP 初始化后传入 audio_codec_handle_t） */
void tts_set_codec(void *codec_handle);

/* 同步朗读中文文本（阻塞直到播放完成）；返回是否成功 */
bool tts_speak(const char *text);

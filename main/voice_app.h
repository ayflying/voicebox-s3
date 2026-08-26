#pragma once

/* 打开语音助手界面（长按说话） */
void voice_app_open(void);

/* 设置录音/播放用的音频编解码句柄（BSP 初始化后由 app_main 传入） */
void voice_app_set_codec(void *codec_handle);

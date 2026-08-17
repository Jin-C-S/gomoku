#ifndef AUDIO_H
#define AUDIO_H

// 初始化音频系统（读取当前音量设置）
void audio_init();

// 清理音频资源（关闭所有已打开的 MCI 设备）
void audio_cleanup();

// 背景音乐
void audio_play_bgm();             // 开始循环播放（已播放则无操作）
void audio_stop_bgm();             // 暂停背景音乐（保留设备，可快速恢复）
void audio_set_bgm_volume(int vol); // 设置背景音乐音量 0-100

// 音效
void audio_play_place();           // 落子音
void audio_play_win();             // 胜利音
void audio_play_lose();            // 失败音
void audio_set_sfx_volume(int vol); // 设置音效音量 0-100

#endif // AUDIO_H

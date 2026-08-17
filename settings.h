#ifndef SETTINGS_H
#define SETTINGS_H

#include <easyx.h>

// 棋盘颜色主题
#define BOARD_THEME_CLASSIC  0  // 经典木纹（使用 qipan.jpg）
#define BOARD_THEME_DARK     1  // 深色系
#define BOARD_THEME_LIGHT    2  // 浅色系

// 棋子材质
#define PIECE_MATERIAL_CLASSIC  0  // 经典黑白纯色
#define PIECE_MATERIAL_JADE     1  // 玉石质感
#define PIECE_MATERIAL_METAL    2  // 金属质感

typedef struct {
    // 棋盘
    int board_theme;             // 0=经典, 1=深色, 2=浅色
    COLORREF board_line_color;   // 棋盘网格线颜色
    COLORREF board_bg_color;     // 棋盘背景色（非经典主题时使用）

    // 棋子
    int piece_material;          // 0=经典, 1=玉石, 2=金属
    COLORREF black_color;        // 黑子颜色
    COLORREF white_color;        // 白子颜色

    int forbidden_enabled;       // 禁手规则 0=关, 1=开

    // 音频
    int bgm_enabled;             // 背景音乐 0=关, 1=开
    int bgm_volume;              // 音量 0-100
    int sfx_place_enabled;       // 落子音效
    int sfx_win_enabled;         // 胜负音效
    int sfx_volume;              // 音效音量 0-100

    // 落子提示（仅人机）
    int hint_enabled;            // 0=关, 1=开
    int hint_limit;              // 每局次数限制, 0=不限

    // AI 难度（仅人机）
    int ai_difficulty;           // 0=简单(贪心), 1=困难(alpha-beta剪枝)
} GameSettings;

// AI 难度选项
#define AI_DIFF_EASY   0
#define AI_DIFF_HARD   1

extern GameSettings game_settings;
extern int settings_open;

// 从 settings.ini 加载设置
void settings_load();

// 保存设置到 settings.ini
void settings_save();

// 绘制设置面板
void draw_settings_panel();

// 处理设置面板点击，返回 1=已处理
int handle_settings_click(int x, int y);

#endif // SETTINGS_H

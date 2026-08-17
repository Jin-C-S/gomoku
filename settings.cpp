#include <windows.h>
#include <stdio.h>
#include "settings.h"
#include "gomoku.h"
#include "audio.h"

// 全局变量
GameSettings game_settings;
int settings_open = 0;

// 设置面板几何
#define PANEL_W     480
#define PANEL_H     760     // 容纳 AI 难度分区
#define PANEL_X     ((WINDOW_SIZE - PANEL_W) / 2)
#define PANEL_Y     ((WINDOW_SIZE - PANEL_H) / 2)
#define BTN_SM_W    80
#define BTN_SM_H    32
#define SECTION_GAP 55  // 每个设置区间的间隔

// 设置默认值
static void set_defaults()
{
    game_settings.board_theme       = BOARD_THEME_CLASSIC;
    game_settings.board_line_color  = RGB(60, 40, 20);
    game_settings.board_bg_color    = RGB(220, 190, 150);

    game_settings.piece_material    = PIECE_MATERIAL_CLASSIC;
    game_settings.black_color       = BLACK;
    game_settings.white_color       = WHITE;

    game_settings.bgm_enabled       = 0;
    game_settings.bgm_volume        = 50;
    game_settings.sfx_place_enabled = 1;
    game_settings.sfx_win_enabled   = 1;
    game_settings.sfx_volume        = 80;

    game_settings.forbidden_enabled = 1;   // 默认开启禁手规则

    game_settings.hint_enabled      = 1;
    game_settings.hint_limit        = 3;

    game_settings.ai_difficulty     = AI_DIFF_EASY;   // 默认简单难度
}

// COLORREF <-> hex string (#RRGGBB)
static void color_to_str(COLORREF c, char* out)
{
    sprintf(out, "%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
}

static COLORREF str_to_color(const char* s)
{
    int r = 0, g = 0, b = 0;
    if (s && strlen(s) >= 6)
        sscanf(s, "%2x%2x%2x", &r, &g, &b);
    return RGB(r, g, b);
}

// 加载设置
void settings_load()
{
    wchar_t ini_path[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, ini_path);
    wcscat(ini_path, L"\\settings.ini");

    // 检查文件是否存在，不存在则用默认值
    if (GetFileAttributesW(ini_path) == INVALID_FILE_ATTRIBUTES)
    {
        set_defaults();
        settings_save();
        return;
    }

    // 棋盘
    game_settings.board_theme = GetPrivateProfileIntW(L"Board", L"theme", 0, ini_path);

    char buf[32];
    char tmp[32];

    GetPrivateProfileStringW(L"Board", L"line_color", L"3C2814", (LPWSTR)buf, 32, ini_path);
    wcstombs(tmp, (const wchar_t*)buf, 32);
    game_settings.board_line_color = str_to_color(tmp);

    GetPrivateProfileStringW(L"Board", L"bg_color", L"DABF96", (LPWSTR)buf, 32, ini_path);
    wcstombs(tmp, (const wchar_t*)buf, 32);
    game_settings.board_bg_color = str_to_color(tmp);

    // 棋子
    game_settings.piece_material = GetPrivateProfileIntW(L"Piece", L"material", 0, ini_path);

    GetPrivateProfileStringW(L"Piece", L"black_color", L"000000", (LPWSTR)buf, 32, ini_path);
    wcstombs(tmp, (const wchar_t*)buf, 32);
    game_settings.black_color = str_to_color(tmp);

    GetPrivateProfileStringW(L"Piece", L"white_color", L"FFFFFF", (LPWSTR)buf, 32, ini_path);
    wcstombs(tmp, (const wchar_t*)buf, 32);
    game_settings.white_color = str_to_color(tmp);

    game_settings.forbidden_enabled = GetPrivateProfileIntW(L"Rule", L"forbidden", 1, ini_path);

    // 音频
    game_settings.bgm_enabled       = GetPrivateProfileIntW(L"Audio", L"bgm_enabled", 0, ini_path);
    game_settings.bgm_volume        = GetPrivateProfileIntW(L"Audio", L"bgm_volume", 50, ini_path);
    game_settings.sfx_place_enabled = GetPrivateProfileIntW(L"Audio", L"sfx_place_enabled", 1, ini_path);
    game_settings.sfx_win_enabled   = GetPrivateProfileIntW(L"Audio", L"sfx_win_enabled", 1, ini_path);
    game_settings.sfx_volume        = GetPrivateProfileIntW(L"Audio", L"sfx_volume", 80, ini_path);

    // 落子提示
    game_settings.hint_enabled      = GetPrivateProfileIntW(L"Hint", L"enabled", 1, ini_path);
    game_settings.hint_limit        = GetPrivateProfileIntW(L"Hint", L"limit", 3, ini_path);

    // AI 难度
    game_settings.ai_difficulty     = GetPrivateProfileIntW(L"AI", L"difficulty", AI_DIFF_EASY, ini_path);
}

// 保存设置
void settings_save()
{
    wchar_t ini_path[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, ini_path);
    wcscat(ini_path, L"\\settings.ini");

    wchar_t buf[32];

    // Board
    wsprintfW(buf, L"%d", game_settings.board_theme);
    WritePrivateProfileStringW(L"Board", L"theme", buf, ini_path);

    char tmp[32];
    color_to_str(game_settings.board_line_color, tmp);
    mbstowcs(buf, tmp, 32);
    WritePrivateProfileStringW(L"Board", L"line_color", buf, ini_path);

    color_to_str(game_settings.board_bg_color, tmp);
    mbstowcs(buf, tmp, 32);
    WritePrivateProfileStringW(L"Board", L"bg_color", buf, ini_path);

    // Piece
    wsprintfW(buf, L"%d", game_settings.piece_material);
    WritePrivateProfileStringW(L"Piece", L"material", buf, ini_path);

    color_to_str(game_settings.black_color, tmp);
    mbstowcs(buf, tmp, 32);
    WritePrivateProfileStringW(L"Piece", L"black_color", buf, ini_path);

    color_to_str(game_settings.white_color, tmp);
    mbstowcs(buf, tmp, 32);
    WritePrivateProfileStringW(L"Piece", L"white_color", buf, ini_path);

    wsprintfW(buf, L"%d", game_settings.forbidden_enabled);
    WritePrivateProfileStringW(L"Rule", L"forbidden", buf, ini_path);

    // Audio
    wsprintfW(buf, L"%d", game_settings.bgm_enabled);
    WritePrivateProfileStringW(L"Audio", L"bgm_enabled", buf, ini_path);
    wsprintfW(buf, L"%d", game_settings.bgm_volume);
    WritePrivateProfileStringW(L"Audio", L"bgm_volume", buf, ini_path);
    wsprintfW(buf, L"%d", game_settings.sfx_place_enabled);
    WritePrivateProfileStringW(L"Audio", L"sfx_place_enabled", buf, ini_path);
    wsprintfW(buf, L"%d", game_settings.sfx_win_enabled);
    WritePrivateProfileStringW(L"Audio", L"sfx_win_enabled", buf, ini_path);
    wsprintfW(buf, L"%d", game_settings.sfx_volume);
    WritePrivateProfileStringW(L"Audio", L"sfx_volume", buf, ini_path);

    // Hint
    wsprintfW(buf, L"%d", game_settings.hint_enabled);
    WritePrivateProfileStringW(L"Hint", L"enabled", buf, ini_path);
    wsprintfW(buf, L"%d", game_settings.hint_limit);
    WritePrivateProfileStringW(L"Hint", L"limit", buf, ini_path);

    // AI
    wsprintfW(buf, L"%d", game_settings.ai_difficulty);
    WritePrivateProfileStringW(L"AI", L"difficulty", buf, ini_path);
}

// ---- 辅助绘制 ----

// 绘制一个带颜色的选择按钮
static void draw_option_btn(int x, int y, int w, int h, const char* text, int selected, int enabled)
{
    if (enabled && selected)
    {
        setfillcolor(RGB(80, 140, 200));
        setlinecolor(RGB(50, 100, 160));
    }
    else if (enabled)
    {
        setfillcolor(RGB(200, 200, 200));
        setlinecolor(RGB(150, 150, 150));
    }
    else
    {
        setfillcolor(RGB(180, 180, 180));
        setlinecolor(RGB(140, 140, 140));
    }
    fillrectangle(x, y, x + w, y + h);

    settextcolor(enabled ? RGB(30, 30, 30) : RGB(120, 120, 120));
    settextstyle(16, 0, "黑体");
    setbkmode(TRANSPARENT);
    int tw = textwidth(text);
    int tx = x + (w - tw) / 2;
    outtextxy(tx, y + (h - 16) / 2, text);
}

// 绘制设置面板
void draw_settings_panel()
{
    // 半透明遮罩（ARGB 格式）
    setfillcolor(0xAA000000);
    solidrectangle(0, 0, WINDOW_SIZE, WINDOW_SIZE);

    // 设置卡片背景
    setfillcolor(RGB(245, 235, 215));
    setlinecolor(RGB(100, 80, 50));
    setlinestyle(PS_SOLID, 2);
    fillrectangle(PANEL_X, PANEL_Y, PANEL_X + PANEL_W, PANEL_Y + PANEL_H);

    char buf[64];

    // 标题
    settextcolor(RGB(60, 40, 20));
    settextstyle(28, 0, "黑体");
    setbkmode(TRANSPARENT);
    outtextxy(PANEL_X + 20, PANEL_Y + 15, "游戏设置");

    // 关闭按钮 (X)
    int close_x = PANEL_X + PANEL_W - 40;
    int close_y = PANEL_Y + 12;
    setfillcolor(RGB(200, 100, 100));
    setlinecolor(RGB(160, 60, 60));
    fillrectangle(close_x, close_y, close_x + 28, close_y + 28);
    settextcolor(WHITE);
    settextstyle(20, 0, "黑体");
    outtextxy(close_x + 7, close_y + 4, "×");

    int x0 = PANEL_X + 30;    // 左对齐位置
    int y_cursor = PANEL_Y + 55;

    settextstyle(18, 0, "黑体");
    settextcolor(RGB(60, 40, 20));

    // ---- 1. 棋盘大小 ----
    outtextxy(x0, y_cursor, "棋盘大小");
    y_cursor += 28;
    int sizes[] = {9, 13, 15};
    for (int i = 0; i < 3; i++)
    {
        sprintf(buf, "%d×%d", sizes[i], sizes[i]);
        draw_option_btn(x0 + i * (BTN_SM_W + 10), y_cursor, BTN_SM_W, BTN_SM_H,
                        buf, BOARD_SIZE == sizes[i], 1);
    }

    // ---- AI 难度 ----
    y_cursor += BTN_SM_H + 20;
    outtextxy(x0, y_cursor, "AI难度（仅人机）");
    y_cursor += 28;
    const char* diff_labels[] = {"简单", "困难"};
    for (int i = 0; i < 2; i++)
    {
        draw_option_btn(x0 + i * (BTN_SM_W + 10), y_cursor, BTN_SM_W, BTN_SM_H,
                        diff_labels[i], game_settings.ai_difficulty == i, 1);
    }

        // ---- 6. 禁手规则 ----
        y_cursor += BTN_SM_H + 20;
        outtextxy(x0, y_cursor, "禁手规则");
        y_cursor += 28;
        draw_option_btn(x0, y_cursor, BTN_SM_W, BTN_SM_H,
                        game_settings.forbidden_enabled ? "开" : "关", game_settings.forbidden_enabled, 1);

    // ---- 2. 棋盘主题 ----
    y_cursor += BTN_SM_H + 20;
    outtextxy(x0, y_cursor, "棋盘主题");
    y_cursor += 28;
    const char* themes[] = {"经典", "深色", "浅色"};
    for (int i = 0; i < 3; i++)
    {
        draw_option_btn(x0 + i * (BTN_SM_W + 10), y_cursor, BTN_SM_W, BTN_SM_H,
                        themes[i], game_settings.board_theme == i, 1);
    }

    // ---- 3. 棋子材质 ----
    y_cursor += BTN_SM_H + 20;
    outtextxy(x0, y_cursor, "棋子材质");
    y_cursor += 28;
    const char* materials[] = {"经典", "玉石", "金属"};
    for (int i = 0; i < 3; i++)
    {
        draw_option_btn(x0 + i * (BTN_SM_W + 10), y_cursor, BTN_SM_W, BTN_SM_H,
                        materials[i], game_settings.piece_material == i, 1);
    }

    // ---- 4. 背景音乐 ----
    y_cursor += BTN_SM_H + 20;
    outtextxy(x0, y_cursor, "背景音乐");
    y_cursor += 28;
    // 开关
    draw_option_btn(x0, y_cursor, BTN_SM_W, BTN_SM_H,
                    game_settings.bgm_enabled ? "开" : "关", game_settings.bgm_enabled, 1);
    // 音量阶梯
    int vol_x = x0 + BTN_SM_W + 20;
    int vol_levels[] = {0, 25, 50, 75, 100};
    for (int i = 0; i < 5; i++)
    {
        sprintf(buf, "%d", vol_levels[i]);
        draw_option_btn(vol_x + i * (40 + 6), y_cursor, 40, BTN_SM_H,
                        buf, game_settings.bgm_volume == vol_levels[i],
                        game_settings.bgm_enabled);
    }

    // ---- 5. 音效 ----
    y_cursor += BTN_SM_H + 20;
    outtextxy(x0, y_cursor, "落子/胜负音效");
    y_cursor += 28;
    draw_option_btn(x0, y_cursor, BTN_SM_W, BTN_SM_H,
                    game_settings.sfx_place_enabled ? "开" : "关",
                    game_settings.sfx_place_enabled, 1);
    vol_x = x0 + BTN_SM_W + 20;
    for (int i = 0; i < 5; i++)
    {
        sprintf(buf, "%d", vol_levels[i]);
        draw_option_btn(vol_x + i * (40 + 6), y_cursor, 40, BTN_SM_H,
                        buf, game_settings.sfx_volume == vol_levels[i],
                        game_settings.sfx_place_enabled);
    }

    // ---- 6. 落子提示 ----
    y_cursor += BTN_SM_H + 20;
    outtextxy(x0, y_cursor, "落子提示（仅人机）");
    y_cursor += 28;
    draw_option_btn(x0, y_cursor, BTN_SM_W, BTN_SM_H,
                    game_settings.hint_enabled ? "开" : "关", game_settings.hint_enabled, 1);
    // 次数限制选择
    int hint_x = x0 + BTN_SM_W + 20;
    int hint_limits[] = {0, 1, 3, 5, 10};
    const char* hint_labels[] = {"不限", "1次", "3次", "5次", "10"};
    for (int i = 0; i < 5; i++)
    {
        draw_option_btn(hint_x + i * (50 + 8), y_cursor, 50, BTN_SM_H,
                        hint_labels[i], game_settings.hint_limit == hint_limits[i],
                        game_settings.hint_enabled);
    }

    // ---- 底部关闭按钮 ----
    int btn_close_x = PANEL_X + (PANEL_W - 120) / 2;
    int btn_close_y = PANEL_Y + PANEL_H - 48;
    setfillcolor(RGB(160, 130, 100));
    setlinecolor(RGB(130, 100, 70));
    fillrectangle(btn_close_x, btn_close_y, btn_close_x + 120, btn_close_y + 36);
    settextcolor(WHITE);
    settextstyle(18, 0, "黑体");
    int tw = textwidth("关闭");
    outtextxy(btn_close_x + (120 - tw) / 2, btn_close_y + 9, "关闭");
}

// ---- 点击处理 ----

int handle_settings_click(int x, int y)
{
    // 关闭按钮 (X)
    int close_x = PANEL_X + PANEL_W - 40;
    int close_y = PANEL_Y + 12;
    if (x >= close_x && x <= close_x + 28 && y >= close_y && y <= close_y + 28)
    {
        settings_save();
        settings_open = 0;
        draw_full();
        return 1;
    }

    // 底部关闭按钮
    int btn_close_x = PANEL_X + (PANEL_W - 120) / 2;
    int btn_close_y = PANEL_Y + PANEL_H - 48;
    if (x >= btn_close_x && x <= btn_close_x + 120 &&
        y >= btn_close_y && y <= btn_close_y + 36)
    {
        settings_save();
        settings_open = 0;
        draw_full();
        return 1;
    }

    // 点击遮罩外部（面板之外的区域）→ 关闭
    if (x < PANEL_X || x > PANEL_X + PANEL_W || y < PANEL_Y || y > PANEL_Y + PANEL_H)
    {
        settings_save();
        settings_open = 0;
        draw_full();
        return 1;
    }

    // 辅助：检测 n 个水平排列的小按钮
    // 参数：起始绝对坐标 (bx,by)，按钮宽度 w，按钮高度 h，间距 gap，数量 count
    // 返回选中索引（0-based），未命中返回 -1
    // 按钮几何必须与 draw_settings_panel 中一致

    int x0 = PANEL_X + 30;
    int y_cursor = PANEL_Y + 55;

    // ---- 1. 棋盘大小 (y偏移: 标题28 + 按钮行BTN_SM_H) ----
    y_cursor += 28;  // 标题行
    for (int i = 0; i < 3; i++)
    {
        int bx = x0 + i * (BTN_SM_W + 10);
        if (x >= bx && x <= bx + BTN_SM_W && y >= y_cursor && y <= y_cursor + BTN_SM_H)
        {
            int new_size = (i == 0) ? 9 : (i == 1) ? 13 : 15;
            if (BOARD_SIZE != new_size)
            {
                BOARD_SIZE = new_size;
                calc_board_layout();
                init_board();
                settings_open = 1;  // 保持打开
                draw_full();
            }
            return 1;
        }
    }

    // ---- AI 难度 ----
    y_cursor += BTN_SM_H + 20;    // 棋盘大小按钮行结束后的间隔
    y_cursor += 28;                // 标题行
    for (int i = 0; i < 2; i++)
    {
        int bx = x0 + i * (BTN_SM_W + 10);
        if (x >= bx && x <= bx + BTN_SM_W && y >= y_cursor && y <= y_cursor + BTN_SM_H)
        {
            if (game_settings.ai_difficulty != i)
            {
                game_settings.ai_difficulty = i;
                draw_full();
            }
            return 1;
        }
    }

    y_cursor += BTN_SM_H + 20;    // AI 难度按钮行结束后的间隔
    y_cursor += 28;                // 标题行
    if (x >= x0 && x <= x0 + BTN_SM_W && y >= y_cursor && y <= y_cursor + BTN_SM_H)
    {
    game_settings.forbidden_enabled = !game_settings.forbidden_enabled;
    draw_full();
    return 1;
    }

    // ---- 2. 棋盘主题 ----
    y_cursor += BTN_SM_H + 20;  // 按钮行 + 间隔
    outtextxy(x0, y_cursor, "棋盘主题");  // 这只是为了更新 cursor
    y_cursor += 28;
    for (int i = 0; i < 3; i++)
    {
        int bx = x0 + i * (BTN_SM_W + 10);
        if (x >= bx && x <= bx + BTN_SM_W && y >= y_cursor && y <= y_cursor + BTN_SM_H)
        {
            if (game_settings.board_theme != i)
            {
                game_settings.board_theme = i;
                // 根据主题更新颜色
                switch (i)
                {
                case BOARD_THEME_CLASSIC:
                    game_settings.board_line_color = RGB(60, 40, 20);
                    game_settings.board_bg_color   = RGB(220, 190, 150);
                    break;
                case BOARD_THEME_DARK:
                    game_settings.board_line_color = RGB(180, 160, 120);
                    game_settings.board_bg_color   = RGB(40, 30, 20);
                    break;
                case BOARD_THEME_LIGHT:
                    game_settings.board_line_color = RGB(120, 100, 70);
                    game_settings.board_bg_color   = RGB(240, 230, 210);
                    break;
                }
                draw_full();
            }
            return 1;
        }
    }

    // ---- 3. 棋子材质 ----
    y_cursor += BTN_SM_H + 20;
    y_cursor += 28;
    for (int i = 0; i < 3; i++)
    {
        int bx = x0 + i * (BTN_SM_W + 10);
        if (x >= bx && x <= bx + BTN_SM_W && y >= y_cursor && y <= y_cursor + BTN_SM_H)
        {
            if (game_settings.piece_material != i)
            {
                game_settings.piece_material = i;
                switch (i)
                {
                case PIECE_MATERIAL_CLASSIC:
                    game_settings.black_color = BLACK;
                    game_settings.white_color = WHITE;
                    break;
                case PIECE_MATERIAL_JADE:
                    game_settings.black_color = RGB(30, 50, 30);
                    game_settings.white_color = RGB(200, 230, 200);
                    break;
                case PIECE_MATERIAL_METAL:
                    game_settings.black_color = RGB(40, 40, 50);
                    game_settings.white_color = RGB(200, 200, 210);
                    break;
                }
                draw_full();
            }
            return 1;
        }
    }

    // ---- 4. 背景音乐 ----
    y_cursor += BTN_SM_H + 20;
    y_cursor += 28;
    // 开关
    if (x >= x0 && x <= x0 + BTN_SM_W && y >= y_cursor && y <= y_cursor + BTN_SM_H)
    {
        game_settings.bgm_enabled = !game_settings.bgm_enabled;
        if (game_settings.bgm_enabled) audio_play_bgm();
        else audio_stop_bgm();
        draw_full();
        return 1;
    }
    // 音量
    int vol_x = x0 + BTN_SM_W + 20;
    int vol_levels[] = {0, 25, 50, 75, 100};
    for (int i = 0; i < 5; i++)
    {
        int bx = vol_x + i * (40 + 6);
        if (x >= bx && x <= bx + 40 && y >= y_cursor && y <= y_cursor + BTN_SM_H)
        {
            if (game_settings.bgm_enabled)
            {
                game_settings.bgm_volume = vol_levels[i];
                audio_set_bgm_volume(game_settings.bgm_volume);
                draw_full();
            }
            return 1;
        }
    }

    // ---- 5. 音效 ----
    y_cursor += BTN_SM_H + 20;
    y_cursor += 28;
    // 开关
    if (x >= x0 && x <= x0 + BTN_SM_W && y >= y_cursor && y <= y_cursor + BTN_SM_H)
    {
        game_settings.sfx_place_enabled = !game_settings.sfx_place_enabled;
        game_settings.sfx_win_enabled = game_settings.sfx_place_enabled;
        if (game_settings.sfx_place_enabled) audio_play_place();  // 试听一声
        draw_full();
        return 1;
    }
    // 音量
    vol_x = x0 + BTN_SM_W + 20;
    for (int i = 0; i < 5; i++)
    {
        int bx = vol_x + i * (40 + 6);
        if (x >= bx && x <= bx + 40 && y >= y_cursor && y <= y_cursor + BTN_SM_H)
        {
            if (game_settings.sfx_place_enabled)
            {
                game_settings.sfx_volume = vol_levels[i];
                audio_set_sfx_volume(game_settings.sfx_volume);
                audio_play_place();  // 试听
                draw_full();
            }
            return 1;
        }
    }

    // ---- 6. 落子提示 ----
    y_cursor += BTN_SM_H + 20;
    y_cursor += 28;
    // 开关
    if (x >= x0 && x <= x0 + BTN_SM_W && y >= y_cursor && y <= y_cursor + BTN_SM_H)
    {
        game_settings.hint_enabled = !game_settings.hint_enabled;
        draw_full();
        return 1;
    }
    // 次数限制
    int hint_x = x0 + BTN_SM_W + 20;
    int hint_limits[] = {0, 1, 3, 5, 10};
    for (int i = 0; i < 5; i++)
    {
        int bx = hint_x + i * (50 + 8);
        if (x >= bx && x <= bx + 50 && y >= y_cursor && y <= y_cursor + BTN_SM_H)
        {
            if (game_settings.hint_enabled)
            {
                game_settings.hint_limit = hint_limits[i];
                draw_full();
            }
            return 1;
        }
    }

    return 0;  // 未处理
}

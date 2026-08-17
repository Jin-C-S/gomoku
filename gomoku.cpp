#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <stdio.h>
#include <wchar.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <windows.h>
#include "gomoku.h"
#include "ai.h"
#include "network.h"
#include "settings.h"
#include "replay.h"
#include <mmsystem.h>
#include "audio.h"   // 音频

// 兼容新版 MinGW 与 EasyX 的链接问题
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
extern "C" FILE* __cdecl __iob_func(void);
#pragma GCC diagnostic pop

extern "C" FILE* __cdecl __iob_func(void)
{
    static FILE iob[3];
    iob[0] = *stdin;
    iob[1] = *stdout;
    iob[2] = *stderr;
    return iob;
}

// 提供 __imp_ 别名以满足 EasyX 的 dllimport 引用
extern "C" FILE* (__cdecl *const __imp___iob_func)(void) = __iob_func;

// 全局变量定义
int BOARD_SIZE = 15;        // 当前棋盘大小 (9/13/15)
int CELL_SIZE  = 38;        // 格子大小（根据 BOARD_SIZE 动态计算）
int BOARD_PX   = 0;         // 棋盘占用像素
int OFFSET_Y   = 0;         // 棋盘上边距（垂直居中）

// 时间控制
#define TIME_TOTAL      300     // 每方总时间 5 分钟（秒）
#define TIME_PER_MOVE   30      // 步时限制 30 秒

int player_total_time[3];       // 剩余总时间，下标 1=黑棋，2=白棋
DWORD move_start_tick;          // 当前这一步开始的时间戳

// 棋盘状态: 0=空, 1=黑子, 2=白子
int board[MAX_BOARD][MAX_BOARD];
int current_player = 1;     // 1=黑棋, 2=白棋
int move_count = 0;         // 步数
int game_mode = 0;          // 0=人人对弈, 1=人机对弈, 2=网络对弈
int game_running = 0;       // 0=未开始, 1=进行中
int game_started = 0;       // 0=选单阶段, 1=已开始过游戏
IMAGE bg_img;               // 背景图片（全局只加载一次）

// 落子提示（仅人机模式）
int hint_row = -1;
int hint_col = -1;
int hint_remain = 0;

// 提示按钮位于棋盘正下方
#define HINT_BTN_W  110
#define HINT_BTN_H  36

// 历史记录（悔棋用）
int move_history_row[MAX_MOVES];
int move_history_col[MAX_MOVES];

void init_timer();              // 初始化计时器
int  check_timeout();           // 检测是否超时，返回 1=超时
void draw_timer_panel();        // 仅更新计时器UI区域（实时倒计时用）
void draw_hint_marker();
void draw_hint_button();
void init_hint();
void do_hint();
void draw_move_labels();

void init_timer()
{
    player_total_time[1] = TIME_TOTAL;
    player_total_time[2] = TIME_TOTAL;
    move_start_tick = GetTickCount();
}

int check_timeout()
{
    if (net_waiting_response) return 0;  // 等待回应时不扣时间

    DWORD now = GetTickCount();
    int player = current_player;

    // 计算当前这一步已用时间（秒）
    int elapsed = (int)((now - move_start_tick) / 1000);

    // 检查步时
    if (elapsed >= TIME_PER_MOVE)
    {
        // 当前玩家步时超时
        return player;  // 返回超时的玩家编号
    }

    // 检查总时间
    if (player_total_time[player] - elapsed <= 0)
    {
        return player;  // 总时间用尽
    }

    return 0;  // 未超时
}

// 撤销最近的 N 步落子
void undo_last_moves(int count)
{
    count = (count > move_count) ? move_count : count;
    for (int i = 0; i < count; i++)
    {
        int r = move_history_row[move_count - 1];
        int c = move_history_col[move_count - 1];
        board[r][c] = 0;
        move_count--;
        current_player = (current_player == 1) ? 2 : 1;
    }
}

// 重新计算棋盘几何参数
void calc_board_layout()
{
    CELL_SIZE = (WINDOW_SIZE - 2 * MARGIN - OFFSET_X) / (BOARD_SIZE - 1);
    if (CELL_SIZE > 45) CELL_SIZE = 45;
    BOARD_PX = MARGIN * 2 + (BOARD_SIZE - 1) * CELL_SIZE;
    OFFSET_Y = (WINDOW_SIZE - BOARD_PX) / 2;
}

// 初始化棋盘:清空棋盘，恢复
void init_board()
{
    for (int i = 0; i < BOARD_SIZE; i++)
        for (int j = 0; j < BOARD_SIZE; j++)
            board[i][j] = 0;
    move_count = 0;
    current_player = 1;
}

// ============================================================
// 棋子渲染：预渲染贴图（径向光照 + 高光 + 抗锯齿 + 投影）
// ============================================================
#define PIECE_M  4        // 贴图四周留白（容纳投影与抗锯齿边）

IMAGE piece_black, piece_white;      // 预渲染的黑/白棋子贴图
static int piece_img_key = -1;       // 上次渲染的“参数签名”

static int clamp255(double v)
{
    int n = (int)(v + 0.5);
    return n < 0 ? 0 : (n > 255 ? 255 : n);
}

// 棋盘大小、材质或棋子/棋盘颜色变化时，需要重建贴图
static int piece_render_key()
{
    int k = CELL_SIZE;
    k = k * 31 + (int)game_settings.piece_material;
    k = k * 31 + (int)game_settings.board_bg_color;
    k = k * 31 + (int)game_settings.black_color;
    k = k * 31 + (int)game_settings.white_color;
    return k;
}

// 每种材质的光照参数
typedef struct
{
    double amb;     // 环境光：决定棋子最低亮度（白子要高，保证够白；黑子要低，保证够黑）
    double diff;    // 漫反射：受光源影响的程度
    double spec;    // 镜面高光强度
    double shine;   // 高光锐利度（越大高光越尖）
    int    env;     // 环境反光：黑子的立体感
    int    floor;   // 底部反光：金属反射桌面
    int    sub;     // 次表面透光：玉石的透亮感
} MatParam;

// 经典=抛光石子，玉石=柔和透光，金属=锐利反光
static MatParam mat_params(int black)
{
    MatParam p;
    switch (game_settings.piece_material)
    {
    case PIECE_MATERIAL_JADE:   // 玉石：柔和宽高光 + 本体透光
        if (black) p = {0.12, 0.45, 0.85, 14, 34, 4, 4};
        else       p = {0.80, 0.26, 0.85, 14,  0, 4, 18};
        break;
    case PIECE_MATERIAL_METAL:  // 金属：锐利高光 + 低漫反射 + 地面反光
        if (black) p = {0.14, 0.28, 1.20, 60, 16, 65, 0};
        else       p = {0.76, 0.22, 1.20, 60,  0, 60, 0};
        break;
    default:                    // 经典：中高光 + 平衡漫反射
        if (black) p = {0.09, 0.52, 1.00, 40, 22, 14, 0};
        else       p = {0.80, 0.30, 1.00, 40,  0, 12, 0};
        break;
    }
    return p;
}

// 逐像素渲染一颗棋子：材质光照 + 高光 + 抗锯齿 + 投影
static void render_piece_image(IMAGE* img, int black)
{
    int r     = CELL_SIZE / 2 - 3;        // 半径（与原来一致）
    int cx    = r + PIECE_M, cy = cx;     // 圆心在贴图内
    int size  = 2 * (r + PIECE_M);

    COLORREF base = black ? game_settings.black_color
                          : game_settings.white_color;
    int baseR = GetRValue(base), baseG = GetGValue(base), baseB = GetBValue(base);
    int bR = GetRValue(game_settings.board_bg_color);
    int bG = GetGValue(game_settings.board_bg_color);
    int bB = GetBValue(game_settings.board_bg_color);

    // 纯黑棋子漫反射不可见，给一个极暗底色保证立体感（对彩色黑子无影响）
    int eR = baseR > 18 ? baseR : 18;
    int eG = baseG > 18 ? baseG : 18;
    int eB = baseB > 18 ? baseB : 18;

    MatParam mp = mat_params(black);

    Resize(img, size, size);
    SetWorkingImage(img);

    // 1) 铺棋盘底色
    setfillcolor(RGB(bR, bG, bB));
    solidrectangle(0, 0, size - 1, size - 1);

    // 2) 柔和投影：右下角由深到浅、逐层收窄的椭圆
    for (int i = 0; i < 12; i++)
    {
        double k = i / 11.0;
        double w = (1.0 - k) * (r + 2);
        int gR = (int)(bR * (1.0 - 0.45 * (1.0 - k)));
        int gG = (int)(bG * (1.0 - 0.45 * (1.0 - k)));
        int gB = (int)(bB * (1.0 - 0.45 * (1.0 - k)));
        setfillcolor(RGB(gR, gG, gB));
        solidellipse((int)(cx + 2 - w), (int)(cy + 3 - w * 0.8),
                     (int)(cx + 2 + w), (int)(cy + 3 + w * 0.8));
    }

    // 3) 棋子本体：逐像素材质光照（左上光源）
    const double Lx = -0.55, Ly = -0.55, Lz = 0.63;   // 光源方向
    const double hd = 1.8061;                          // |(-0.55,-0.55,1.63)|
    const double Hx = -0.55/hd, Hy = -0.55/hd, Hz = 1.63/hd;

    for (int y = 0; y < size; y++)
    {
        for (int x = 0; x < size; x++)
        {
            double dx = x - cx, dy = y - cy;
            double dist = sqrt(dx * dx + dy * dy);
            if (dist >= r + 1.0) continue;

            double nx = dx / r, ny = dy / r;           // 球面法线
            double nz = (dist < r) ? sqrt(1 - nx*nx - ny*ny) : 0;

            // 漫反射 + 环境光（白子高环境光保持亮白，黑子低环境光保持漆黑）
            double lambert = nx * Lx + ny * Ly + nz * Lz;
            if (lambert < 0) lambert = 0;
            double shade = mp.amb + mp.diff * lambert;
            if (shade > 1.0) shade = 1.0;

            // 镜面高光
            double ndh = nx * Hx + ny * Hy + nz * Hz;
            double spec = (ndh > 0) ? mp.spec * pow(ndh, mp.shine) : 0.0;
            if (spec > 1.0) spec = 1.0;

            // 环境反光（黑子）/ 底部反光（金属）/ 次表面透光（玉石，按本色染色）
            double env   = black ? mp.env * nz : 0.0;
            double floor = (ny < 0) ? mp.floor * (-ny) : 0.0;
            double subR  = baseR * mp.sub * nz / 255.0;
            double subG  = baseG * mp.sub * nz / 255.0;
            double subB  = baseB * mp.sub * nz / 255.0;

            int R = clamp255(eR * shade + 255 * spec + env + floor + subR);
            int G = clamp255(eG * shade + 255 * spec + env + floor + subG);
            int B = clamp255(eB * shade + 255 * spec + env + floor + subB);

            if (dist > r - 1.5)                        // 边缘抗锯齿
            {
                double a = (r + 1.5 - dist) / 3.0;
                R = (int)(R * a + bR * (1 - a));
                G = (int)(G * a + bG * (1 - a));
                B = (int)(B * a + bB * (1 - a));
            }

            putpixel(x, y, RGB(R, G, B));
        }
    }

    SetWorkingImage(NULL);
}

static void ensure_piece_images()
{
    int key = piece_render_key();
    if (key == piece_img_key) return;
    render_piece_image(&piece_black, 1);
    render_piece_image(&piece_white, 0);
    piece_img_key = key;
}

// 在指定位置画棋子（贴预渲染贴图，很快）
void draw_piece(int row, int col, int color)
{
    ensure_piece_images();

    int x = OFFSET_X + MARGIN + col * CELL_SIZE;
    int y = OFFSET_Y + MARGIN + row * CELL_SIZE;
    int r = CELL_SIZE / 2 - 3;
    putimage(x - (r + PIECE_M), y - (r + PIECE_M),
             color == 1 ? &piece_black : &piece_white, SRCCOPY);
}

// 统计某个方向连续同色棋子
int count_in_direction(int row, int col, int dr, int dc, int color)
{
    int count = 0;
    int r = row + dr, c = col + dc;
    while (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE
           && board[r][c] == color)
    {
        count++;
        r += dr;
        c += dc;
    }
    return count;
}

// 五子连珠检测
int check_win(int row, int col, int color)
{
    int dir[4][2] = {{0,1}, {1,0}, {1,1}, {1,-1}};
    for (int d = 0; d < 4; d++)
    {
        int total = 1
            + count_in_direction(row, col, dir[d][0], dir[d][1], color)
            + count_in_direction(row, col, -dir[d][0], -dir[d][1], color);
        if (total >= 5) return 1;
    }
    return 0;
}

// 检查落子合法性
int is_valid_move(int row, int col)
{
    return (row >= 0 && row < BOARD_SIZE && col >= 0 && col < BOARD_SIZE
            && board[row][col] == 0);
}

// ---- 禁手检测 ----

// 分析落子后在某个方向形成的连子情况
// 参数: 落子位置(row,col), 方向(dr,dc), 当前颜色
// 输出: count=连子数, open_ends=开放端数(0/1/2)
void analyze_line(int row, int col, int dr, int dc, int color,
                  int* count, int* open_ends)
{
    *count = 1;  // 当前落子
    int r, c;

    // 正方向统计
    r = row + dr;
    c = col + dc;
    while (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE
           && board[r][c] == color)
    {
        (*count)++;
        r += dr;
        c += dc;
    }
    int forward_open = (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE
                        && board[r][c] == 0);

    // 反方向统计
    r = row - dr;
    c = col - dc;
    while (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE
           && board[r][c] == color)
    {
        (*count)++;
        r -= dr;
        c -= dc;
    }
    int backward_open = (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE
                         && board[r][c] == 0);

    *open_ends = (forward_open ? 1 : 0) + (backward_open ? 1 : 0);
}

// 检查落子是否构成禁手（仅对黑棋适用）
// 返回: 1=禁手, 0=合法
int is_forbidden(int row, int col, int color)
{
    if (color == 2) return 0;  // 白棋无禁手

    int dirs[4][2] = {{0,1}, {1,0}, {1,1}, {1,-1}};
    int live_threes = 0;
    int fours = 0;

    for (int d = 0; d < 4; d++)
    {
        int count, open_ends;
        analyze_line(row, col, dirs[d][0], dirs[d][1], color,
                     &count, &open_ends);

        // 长连禁手：连子数 >= 6
        if (count >= 6) return 1;

        // 统计四子（活四 open_ends==2 或冲四 open_ends==1）
        if (count == 4 && open_ends >= 1) fours++;

        // 统计活三（连三且两端开放）
        if (count == 3 && open_ends == 2) live_threes++;
    }

    // 四四禁手：同时形成2个及以上四子
    if (fours >= 2) return 1;

    // 三三禁手：同时形成2个及以上活三
    if (live_threes >= 2) return 1;

    return 0;
}

// 绘制棋盘网格（不含背景和棋子）
void draw_board_grid()
{
    // 非经典主题时填充棋盘背景色
    if (game_settings.board_theme != BOARD_THEME_CLASSIC)
    {
        int bg_x = OFFSET_X + MARGIN - CELL_SIZE / 2;
        int bg_y = OFFSET_Y + MARGIN - CELL_SIZE / 2;
        int bg_w = (BOARD_SIZE - 1) * CELL_SIZE + CELL_SIZE;
        int bg_h = bg_w;
        setfillcolor(game_settings.board_bg_color);
        setlinecolor(TRANSPARENT);
        solidrectangle(bg_x, bg_y, bg_x + bg_w, bg_y + bg_h);
    }

    setlinecolor(game_settings.board_line_color);
    setlinestyle(PS_SOLID, 2);
    for (int i = 0; i < BOARD_SIZE; i++)
    {
        int px = OFFSET_X + MARGIN + i * CELL_SIZE;
        int py = OFFSET_Y + MARGIN + i * CELL_SIZE;
        line(OFFSET_X + MARGIN, py,
             OFFSET_X + MARGIN + (BOARD_SIZE - 1) * CELL_SIZE, py);
        line(px, OFFSET_Y + MARGIN,
             px, OFFSET_Y + MARGIN + (BOARD_SIZE - 1) * CELL_SIZE);
    }

    // 星位仅标准 15x15 棋盘使用
    if (BOARD_SIZE == 15)
    {
        int star[5][2] = {{7,7}, {3,3}, {3,11}, {11,3}, {11,11}};
        setfillcolor(game_settings.board_line_color);
        for (int i = 0; i < 5; i++)
        {
            int x = OFFSET_X + MARGIN + star[i][0] * CELL_SIZE;
            int y = OFFSET_Y + MARGIN + star[i][1] * CELL_SIZE;
            solidcircle(x, y, 5);
        }
    }
}

// 绘制左侧 UI 面板
void draw_ui_panel()
{
    setbkmode(TRANSPARENT);
    char buf[32];

    // 标题
    settextcolor(RGB(60, 40, 20));
    settextstyle(56, 0, "黑体");
    outtextxy(30, 20, "五子棋");

    // 当前执棋
    settextstyle(22, 0, "黑体");
    outtextxy(BTN_X, 80, "当前执棋：");
    if (game_started)
    {
        int icon_x = BTN_X + 130;
        int icon_y = 97;
        if (current_player == 1)
        {
            setfillcolor(BLACK);
            setlinecolor(BLACK);
            solidcircle(icon_x, icon_y, 14);
            settextcolor(BLACK);
            outtextxy(icon_x + 24, icon_y - 10, "黑棋");
        }
        else
        {
            setfillcolor(WHITE);
            setlinecolor(DARKGRAY);
            solidcircle(icon_x, icon_y, 14);
            settextcolor(RGB(80, 80, 80));
            outtextxy(icon_x + 24, icon_y - 10, "白棋");
        }
    }
    else
    {
        settextcolor(RGB(160, 120, 80));
        outtextxy(BTN_X + 130, 85, "请开始游戏");
    }

    // 清除步数 + 计时器 + 模式 + 状态文本区域的背景（防止单独调用时文字残留）
    putimage(BTN_X, 125, BTN_W, 105, &bg_img, BTN_X, 125, SRCCOPY);

    // 步数
    settextstyle(22, 0, "黑体");
    settextcolor(RGB(60, 40, 20));
    sprintf(buf, "步数：%d", move_count);
    outtextxy(BTN_X, 135, buf);

    // 显示双方剩余时间（实时倒计时）
    settextstyle(16, 0, "黑体");

    // 计算实时显示时间（当前玩家扣除已用时间）
    int display_time[3] = {0, player_total_time[1], player_total_time[2]};
    if (game_running && !net_waiting_response)
    {
        DWORD now = GetTickCount();
        int elapsed = (int)((now - move_start_tick) / 1000);
        display_time[current_player] = player_total_time[current_player] - elapsed;
        if (display_time[current_player] < 0) display_time[current_player] = 0;
    }

    sprintf(buf, "黑棋：%d分%02d秒", display_time[1] / 60, display_time[1] % 60);
    outtextxy(BTN_X, 160, buf);
    sprintf(buf, "白棋：%d分%02d秒", display_time[2] / 60, display_time[2] % 60);
    outtextxy(BTN_X, 175, buf);

    // 模式
    settextstyle(20, 0, "黑体");
    const char* mode_str = "模式：人人对弈";
    if (game_mode == 1) mode_str = "模式：人机对弈";
    if (game_mode == 2) mode_str = "模式：网络对弈";
    outtextxy(BTN_X, 195, mode_str);

    // 游戏状态 / 网络连接状态
    settextstyle(18, 0, "黑体");
    if (game_mode == GAME_MODE_NETWORK)
    {
        settextcolor(RGB(200, 60, 60));
        if (!network_connected)
            outtextxy(BTN_X, 210, network_role == 1 ? "等待对手连接..." : "正在连接...");
        else
            outtextxy(BTN_X, 210, "网络已连接，白棋后手");
    }
    else
    {
        settextcolor(RGB(60, 40, 20));
        if (!game_started)
            outtextxy(BTN_X, 210, "状态：选单 - 点击开始");
        else if (!game_running)
            outtextxy(BTN_X, 210, "状态：已结束");
        else
            outtextxy(BTN_X, 210, "状态：进行中");
    }

    // ---- 绘制按钮 ----
    int btn_y[] = {240, 295, 350, 400, 450, 500, 550, 600};
    const char* btn_text[] = { game_running ? "游戏中..." : "开始游戏",
                               "重新开始", "选择模式", "设置",
                               "回放存档", "悔棋", "求和", "退出游戏" };

    for (int i = 0; i < 8; i++)
    {
        if (i == 7)  // 退出按钮用不同颜色
        {
            setfillcolor(RGB(200, 120, 120));
            setlinecolor(RGB(160, 80, 80));
        }
        else if (i == 0)  // 开始游戏按钮
        {
            if (game_running)
            {
                setfillcolor(RGB(160, 160, 160));  // 游戏中 - 灰色
                setlinecolor(RGB(130, 130, 130));
            }
            else
            {
                setfillcolor(RGB(100, 180, 100));  // 可点击 - 绿色
                setlinecolor(RGB(70, 150, 70));
            }
        }
        else
        {
            setfillcolor(RGB(200, 180, 140));
            setlinecolor(RGB(160, 130, 80));
        }
        setlinestyle(PS_SOLID, 1);
        fillrectangle(BTN_X, btn_y[i], BTN_X + BTN_W, btn_y[i] + BTN_H);

        settextcolor(RGB(60, 40, 20));
        settextstyle(18, 0, "黑体");
        int tw = textwidth(btn_text[i]);
        int tx = BTN_X + (BTN_W - tw) / 2;
        outtextxy(tx, btn_y[i] + (BTN_H - 18) / 2, btn_text[i]);
    }

    // 棋盘大小切换提示（已移入设置面板）
    // 保留此行作为占位，实际不再输出
}

// 处理按钮点击 - 返回 1=已处理, 0=未点击按钮
int handle_button_click(int x, int y)
{
    int btn_y[] = {240, 295, 350, 400, 450, 500, 550, 600};

    // 开始游戏
    if (x >= BTN_X && x <= BTN_X + BTN_W && y >= btn_y[0] && y <= btn_y[0] + BTN_H)
    {
        if (game_running) return 1;  // 游戏中，无效点击
        net_waiting_response = 0;
        game_started = 1;
        game_running = 1;
        init_board();
        init_timer();
        init_hint();
        if (game_settings.bgm_enabled) audio_play_bgm();  // 恢复背景音乐
        draw_full();
        draw_ui_panel();
        return 1;
    }

    // 重新开始
    if (x >= BTN_X && x <= BTN_X + BTN_W && y >= btn_y[1] && y <= btn_y[1] + BTN_H)
    {
        if (!game_started) return 1;  // 还没开始过，忽略

        net_waiting_response = 0;
        // 网络模式：发送重启信号给对手
        if (game_mode == GAME_MODE_NETWORK && network_connected)
            net_send_move(NET_SIGNAL_RESTART, NET_SIGNAL_RESTART);

        game_running = 1;
        init_board();
        init_timer();
        init_hint();
        if (game_settings.bgm_enabled) audio_play_bgm();  // 恢复背景音乐
        draw_full();
        draw_ui_panel();
        return 1;
    }

    // 选择模式 (弹窗: 是=人人, 否=人机, 取消=网络)
    if (x >= BTN_X && x <= BTN_X + BTN_W && y >= btn_y[2] && y <= btn_y[2] + BTN_H)
    {
        net_waiting_response = 0;
        // 退出网络模式时断开连接
        if (game_mode == GAME_MODE_NETWORK && network_connected)
            net_disconnect();

        int choice = MessageBoxW(GetHWnd(),
            L"请选择对战模式\n\n是 = 人人对弈\n否 = 人机对弈\n取消 = 网络对弈",
            L"选择模式", MB_YESNOCANCEL);

        if (choice == IDYES)
            game_mode = 0;
        else if (choice == IDNO)
            game_mode = 1;
        else if (choice == IDCANCEL)
            game_mode = 2;
        else
            return 1;  // 用户关闭了对话框

        game_started = 0;
        game_running = 0;
        init_board();

        // 进入网络模式：建立连接
        if (game_mode == GAME_MODE_NETWORK)
        {
            int role_choice = MessageBoxW(GetHWnd(),
                L"选择网络角色\n\n确定 = 主机（等待对手连接）\n取消 = 加入（连接对手）",
                L"网络对局", MB_OKCANCEL);
            if (role_choice == IDOK)
            {
                network_role = 1;
                network_connected = net_host_start(NET_PORT);
            }
            else
            {
                network_role = 2;
                printf("请输入对手 IP 地址: ");
                char ip[32] = "127.0.0.1";
                char input[32];
                if (fgets(input, 32, stdin) != NULL)
                {
                    size_t len = strlen(input);
                    if (len > 1 && input[len-1] == '\n') input[len-1] = '\0';
                    if (input[0] != '\0') strncpy(ip, input, 31);
                }
                network_connected = net_client_connect(ip, NET_PORT);
            }
            if (!network_connected)
            {
                MessageBoxW(GetHWnd(), L"连接失败，已退回人人模式", L"网络对局", MB_OK);
                game_mode = 0;
            }
        }

        init_timer();
        draw_full();
        draw_ui_panel();
        return 1;
    }

    // 设置
    if (x >= BTN_X && x <= BTN_X + BTN_W && y >= btn_y[3] && y <= btn_y[3] + BTN_H)
    {
        net_waiting_response = 0;
        settings_open = 1;
        // 如果游戏正在进行，先暂停游戏状态以便设置修改生效
        draw_full();
        return 1;
    }

    // 回放存档
    if (x >= BTN_X && x <= BTN_X + BTN_W && y >= btn_y[4] && y <= btn_y[4] + BTN_H)
    {
        replay_mode = 1;
        draw_full();
        return 1;
    }

    // 悔棋
    if (x >= BTN_X && x <= BTN_X + BTN_W && y >= btn_y[5] && y <= btn_y[5] + BTN_H)
    {
        if (!game_running || move_count <= 0)
        {
            return 1;
        }
        if (game_mode == GAME_MODE_NETWORK && network_connected)
        {
            if (net_waiting_response)
            {
                return 1;  // 已在等待回应
            }
            // 发送悔棋请求
            net_send_move(NET_SIGNAL_UNDO_REQ, NET_SIGNAL_UNDO_REQ);
            net_waiting_response = 1;
        }
        else if (game_mode == 1)  // 人机：撤销 AI 和玩家的各一步
        {
            undo_last_moves(2);
            draw_full();
        }
        else  // 人人：撤销一步
        {
            undo_last_moves(1);
            draw_full();
        }
        return 1;
    }

    // 求和
    if (x >= BTN_X && x <= BTN_X + BTN_W && y >= btn_y[6] && y <= btn_y[6] + BTN_H)
    {
        if (!game_running)
        {
            return 1;
        }
        if (game_mode == GAME_MODE_NETWORK && network_connected)
        {
            if (net_waiting_response)
            {
                return 1;
            }
            net_send_move(NET_SIGNAL_DRAW_REQ, NET_SIGNAL_DRAW_REQ);
            net_waiting_response = 2;
        }
        else if (game_mode == 1)  // 人机
        {
            MessageBoxW(GetHWnd(), L"人机模式下无法和棋", L"求和", MB_OK);
        }
        else  // 人人
        {
            int choice = MessageBoxW(GetHWnd(), L"是否同意和棋？", L"求和", MB_OKCANCEL);
            if (choice == IDOK)
            {
                game_running = 0;
                draw_full();
                MessageBoxW(GetHWnd(), L"双方和棋！", L"求和", MB_OK);
                int save_res = MessageBoxW(GetHWnd(), L"是否保存棋谱？", L"保存棋谱", MB_YESNO);
                if (save_res == IDYES) save_game(3);
                draw_ui_panel();
            }
        }
        return 1;
    }

    // 退出游戏
    if (x >= BTN_X && x <= BTN_X + BTN_W && y >= btn_y[7] && y <= btn_y[7] + BTN_H)
    {
        closegraph();
        exit(0);
        return 1;
    }

    return 0;
}

// 绘制完整画面（背景 + 棋盘 + 棋子 + UI）
void draw_full()
{
    BeginBatchDraw();
    putimage(0, 0, WINDOW_SIZE, WINDOW_SIZE, &bg_img, 0, 0, SRCCOPY);

    draw_board_grid();

    for (int r = 0; r < BOARD_SIZE; r++)
        for (int c = 0; c < BOARD_SIZE; c++)
            if (board[r][c] != 0)
                draw_piece(r, c, board[r][c]);

    draw_ui_panel();
    draw_move_labels();
    draw_hint_marker();     // 在棋子之上绘制提示标记
    draw_hint_button();     // 棋盘正下方的提示按钮

    if (replay_mode == 1) draw_replay_panel();   // 棋谱库面板

    if (replay_mode == 2) draw_replay_controls();

    if (settings_open) draw_settings_panel();

    EndBatchDraw();
}

// ---- 落子顺序标记 ----
// 最后一步：红色圆点  之前的步数：按落子顺序显示 1,2,3...
void draw_move_labels()
{
    if (move_count == 0) return;

    setbkmode(TRANSPARENT);

    int font_size = CELL_SIZE / 3;
    if (font_size < 8) font_size = 8;
    if (font_size > 14) font_size = 14;

    for (int i = 0; i < move_count; i++)
    {
        int r = move_history_row[i];
        int c = move_history_col[i];
        int x = OFFSET_X + MARGIN + c * CELL_SIZE;
        int y = OFFSET_Y + MARGIN + r * CELL_SIZE;

        if (i == move_count - 1)
        {
            setfillcolor(RGB(230, 40, 40));
            setlinecolor(RGB(180, 0, 0));
            solidcircle(x, y, 5);
        }
        else
        {
            char buf[8];
            sprintf(buf, "%d", i + 1);

            if (board[r][c] == 1)
                settextcolor(RGB(220, 220, 220));
            else
                settextcolor(RGB(50, 50, 50));

            settextstyle(font_size, 0, "黑体");
            int tw = textwidth(buf);
            int th = textheight(buf);
            outtextxy(x - tw / 2, y - th / 2, buf);
        }
    }
}

// 仅更新计时器UI区域（实时倒计时用）
void draw_timer_panel()
{
    if (!game_running) return;
    if (net_waiting_response) return;  // 等待回应时暂停倒计时

    DWORD now = GetTickCount();
    int elapsed = (int)((now - move_start_tick) / 1000);
    int player = current_player;

    // 计算双方实时剩余时间
    int remain_current = player_total_time[player] - elapsed;
    if (remain_current < 0) remain_current = 0;
    int remain_other = player_total_time[player == 1 ? 2 : 1];

    BeginBatchDraw();

    // 恢复计时器区域的背景（清除旧的文字）
    putimage(BTN_X, 155, BTN_W, 40, &bg_img, BTN_X, 155, SRCCOPY);

    // 重新绘制计时器文字
    setbkmode(TRANSPARENT);
    settextstyle(16, 0, "黑体");

    char buf[32];
    settextcolor(BLACK);
    sprintf(buf, "黑棋：%d分%02d秒",
            (player == 1 ? remain_current : remain_other) / 60,
            (player == 1 ? remain_current : remain_other) % 60);
    outtextxy(BTN_X, 160, buf);

    settextcolor(RGB(80, 80, 80));
    sprintf(buf, "白棋：%d分%02d秒",
            (player == 2 ? remain_current : remain_other) / 60,
            (player == 2 ? remain_current : remain_other) % 60);
    outtextxy(BTN_X, 175, buf);

    EndBatchDraw();
}

// ---- 落子提示功能 ----

// 提示按钮 X 坐标（棋盘正下方居中）
static int hint_btn_x()
{
    int board_center_x = OFFSET_X + MARGIN + (BOARD_SIZE - 1) * CELL_SIZE / 2;
    return board_center_x - HINT_BTN_W / 2;
}

// 提示按钮 Y 坐标
static int hint_btn_y()
{
    int board_bottom = OFFSET_Y + MARGIN + (BOARD_SIZE - 1) * CELL_SIZE;
    return board_bottom + 15;
}

// 绘制提示标记（绿色十字圆圈）
void draw_hint_marker()
{
    if (hint_row < 0 || hint_col < 0) return;
    int x = OFFSET_X + MARGIN + hint_col * CELL_SIZE;
    int y = OFFSET_Y + MARGIN + hint_row * CELL_SIZE;
    int r = CELL_SIZE / 2 - 2;

    // 绿色圆环
    setlinecolor(RGB(0, 220, 0));
    setlinestyle(PS_SOLID, 3);
    circle(x, y, r);

    // 绿色十字
    int arm = CELL_SIZE / 4;
    line(x - arm, y, x + arm, y);
    line(x, y - arm, x, y + arm);

    setlinestyle(PS_SOLID, 2);  // 恢复线宽
}

// 绘制提示按钮
void draw_hint_button()
{
    // 仅人机模式 + 游戏进行中才显示
    if (game_mode != 1 || !game_running) return;

    int bx = hint_btn_x();
    int by = hint_btn_y();

    // 判断按钮是否可用
    int can_hint = (hint_remain != 0);  // -1=不限, >0=还有剩余

    setfillcolor(can_hint ? RGB(80, 180, 80) : RGB(160, 160, 160));
    setlinecolor(can_hint ? RGB(50, 140, 50) : RGB(130, 130, 130));
    setlinestyle(PS_SOLID, 1);
    fillrectangle(bx, by, bx + HINT_BTN_W, by + HINT_BTN_H);

    // 按钮文字
    char buf[32];
    if (hint_remain == -1)
        sprintf(buf, "提示");
    else if (hint_remain > 0)
        sprintf(buf, "提示(%d)", hint_remain);
    else
        sprintf(buf, "已用尽");

    settextcolor(RGB(255, 255, 255));
    settextstyle(18, 0, "黑体");
    setbkmode(TRANSPARENT);
    int tw = textwidth(buf);
    outtextxy(bx + (HINT_BTN_W - tw) / 2, by + (HINT_BTN_H - 18) / 2, buf);
}

// 初始化提示状态
void init_hint()
{
    hint_row = -1;
    hint_col = -1;
    if (game_settings.hint_enabled && game_mode == 1)
    {
        if (game_settings.hint_limit == 0)
            hint_remain = -1;  // 不限次数
        else
            hint_remain = game_settings.hint_limit;
    }
    else
    {
        hint_remain = 0;  // 关闭
    }
}

// 执行提示：调用 AI 获取推荐位置并显示标记
void do_hint()
{
    if (game_mode != 1 || !game_running) return;
    if (hint_remain == 0) return;  // 已用尽

    int r, c;
    ai_get_move(&r, &c);
    if (r >= 0 && c >= 0)
    {
        hint_row = r;
        hint_col = c;
        if (hint_remain > 0) hint_remain--;  // 有限次数则扣除
        draw_full();
    }
}

int main()
{
    calc_board_layout();
    initgraph(WINDOW_SIZE, WINDOW_SIZE, EX_SHOWCONSOLE);
    loadimage(&bg_img, "qipan.jpg");
    net_init();
    init_board();
    init_timer();
    game_running = 0;
    game_started = 0;

    // 加载设置
    settings_load();
    audio_init();                          // 音频
    if (game_settings.bgm_enabled) audio_play_bgm();

    draw_full();

    while (1)
    {

        // ---- 超时检测 ----
        if (game_running)
        {
        int timeout_player = check_timeout();
        if (timeout_player)
        {
            draw_full();
            wchar_t text[64];
            swprintf(text, 64, L"%ls超时！对手获胜！",
                     timeout_player == 1 ? L"黑棋" : L"白棋");
            MessageBoxW(GetHWnd(), text, L"时间到", MB_OK);

            int save_res = MessageBoxW(GetHWnd(), L"是否保存棋谱？", L"保存棋谱", MB_YESNO);
            if (save_res == IDYES) save_game(timeout_player == 1 ? 2 : 1);

            audio_stop_bgm();
            audio_play_lose();   // 超时失败音效
            game_running = 0;
            draw_ui_panel();
        }
        }


        // ---- 鼠标事件轮询（非阻塞） ----
        ExMessage msg;
        while (peekmessage(&msg, EX_MOUSE))
        {
            if (msg.message == WM_LBUTTONDOWN)
            {
                // 设置面板打开时，优先处理设置面板点击
                if (settings_open)
                {
                    if (handle_settings_click(msg.x, msg.y)) continue;
                    continue;
                }

                // 棋谱库面板打开时，优先处理面板点击
                if (replay_mode == 1)
                {
                    if (handle_replay_click(msg.x, msg.y)) continue;
                    continue;
                }

                // 回放模式：仅处理回放控制按钮
                if (replay_mode == 2)
                {
                    if (handle_replay_control_click(msg.x, msg.y)) continue;
                    continue;
                }

                // 先检测左侧按钮点击
                if (handle_button_click(msg.x, msg.y))
                    continue;

                // 检测提示按钮点击（仅人机模式）
                if (game_mode == 1 && game_running)
                {
                    int hx = hint_btn_x(), hy = hint_btn_y();
                    if (msg.x >= hx && msg.x <= hx + HINT_BTN_W &&
                        msg.y >= hy && msg.y <= hy + HINT_BTN_H)
                    {
                        do_hint();
                        continue;
                    }
                }

                // 游戏未进行时点击棋盘无效（选单或已结束均忽略）
                if (!game_running)
                    continue;

                // 将鼠标坐标映射为棋盘行列
                int col = (msg.x - (OFFSET_X + MARGIN) + CELL_SIZE / 2) / CELL_SIZE;
                int row = (msg.y - (OFFSET_Y + MARGIN) + CELL_SIZE / 2) / CELL_SIZE;

                if (is_valid_move(row, col))
                {
                    // 网络模式：检查是否轮到本方落子
                    if (game_mode == GAME_MODE_NETWORK && network_connected)
                    {
                        int my_color = (network_role == 1) ? 1 : 2;
                        if (current_player != my_color)
                            continue;  // 还没轮到本方，忽略点击
                    }

                    // 等待网络回应时不能落子
                    if (net_waiting_response)
                        continue;

                    // 禁手检测（仅黑棋需检测）
                    //if (current_player == 1 && is_forbidden(row, col, current_player))
                    if (game_settings.forbidden_enabled && current_player == 1 && is_forbidden(row, col, current_player))
                    {
                        MessageBoxW(GetHWnd(), L"禁手！请重新落子", L"五子棋", MB_OK);
                        draw_full();
                        continue;
                    }

                    board[row][col] = current_player;
                    // 落子后清除提示标记
                    hint_row = -1;
                    hint_col = -1;
                    audio_play_place();      // 落子音效
                    move_count++;
                    move_history_row[move_count - 1] = row;
                    move_history_col[move_count - 1] = col;

                    // 网络模式：发送落子给对手
                    if (game_mode == GAME_MODE_NETWORK && network_connected)
                        net_send_move(row, col);

                    // 扣除当前玩家已用时间
                    DWORD now = GetTickCount();
                    int elapsed = (int)((now - move_start_tick) / 1000);
                    player_total_time[current_player] -= elapsed;
                    if (player_total_time[current_player] < 0)
                        player_total_time[current_player] = 0;

                    if (check_win(row, col, current_player))
                    {
                        draw_full();
                        wchar_t text[64];
                        swprintf(text, 64, L"游戏结束！%ls获胜！共%d步",
                                 current_player == 1 ? L"黑棋" : L"白棋", move_count);
                        MessageBoxW(GetHWnd(), text, L"五子棋", MB_OK);

                        audio_stop_bgm();
                        audio_play_win();   // 胜利音效

                        int save_res = MessageBoxW(GetHWnd(), L"是否保存棋谱？", L"保存棋谱", MB_YESNO);
                        if (save_res == IDYES) save_game(current_player == 1 ? 1 : 2);

                        game_running = 0;
                        
                        init_board();
                        init_timer();
                        init_hint();
                        draw_ui_panel();
                    }
                    else
                    {
                        current_player = (current_player == 1) ? 2 : 1;
                        move_start_tick = GetTickCount();//切换落子重置时间
                        draw_full();

                        // 人机模式：轮到 AI（白棋）时自动落子
                        if (game_mode == 1 && current_player == 2 && game_running)
                        {
                            // AI 落子前清除提示标记
                            hint_row = -1;
                            hint_col = -1;
                            Sleep(300);
                            int ai_row, ai_col;
                            ai_get_move(&ai_row, &ai_col);
                            if (ai_row >= 0)
                            {
                                board[ai_row][ai_col] = 2;
                                audio_play_place();  // 落子音效
                                move_count++;
                                move_history_row[move_count - 1] = ai_row;
                                move_history_col[move_count - 1] = ai_col;
                                draw_full();

                                move_start_tick = GetTickCount();//切换落子重置时间

                                if (check_win(ai_row, ai_col, 2))
                                {
                                    draw_full();
                                    MessageBoxW(GetHWnd(), L"游戏结束！AI获胜！",
                                               L"五子棋", MB_OK);
                                    audio_stop_bgm();
                                    audio_play_lose();  // 失败音效

                                    int save_res = MessageBoxW(GetHWnd(), L"是否保存棋谱？", L"保存棋谱", MB_YESNO);
                                    if (save_res == IDYES) save_game(2);

                                    game_running = 0;
                                    init_board();
                                    init_timer();
                                    init_hint();
                                    draw_ui_panel();
                                }
                                else
                                {
                                    current_player = 1;
                                    draw_full();
                                }
                            }
                        }
                    }
                }
                // 点击棋盘外且未进行游戏 → 忽略
                else if (!game_running)
                {
                    continue;
                }
            }
        }

        // ---- 网络对局轮询 ----
        if (game_mode == GAME_MODE_NETWORK && game_running && network_connected)
        {
            NetMove move;
            int ret = net_recv_move(&move);
            if (ret == 1)
            {
                // 重启信号
                if (move.row == NET_SIGNAL_RESTART && move.col == NET_SIGNAL_RESTART)
                {
                    init_board();
                    init_timer();
                    init_hint();
                    current_player = 1;
                    move_count = 0;
                    net_waiting_response = 0;
                    game_running = 1;
                    game_started = 1;
                    draw_full();
                }
                // 悔棋请求
                else if (move.row == NET_SIGNAL_UNDO_REQ && move.col == NET_SIGNAL_UNDO_REQ)
                {
                    int choice = MessageBoxW(GetHWnd(),
                        L"对手请求悔棋，是否同意？", L"悔棋", MB_OKCANCEL);
                    if (choice == IDOK)
                    {
                        net_send_move(NET_SIGNAL_UNDO_ACK, NET_SIGNAL_UNDO_ACK);
                        undo_last_moves(2);
                        draw_full();
                    }
                    else
                    {
                        net_send_move(NET_SIGNAL_UNDO_NACK, NET_SIGNAL_UNDO_NACK);
                    }
                }
                // 悔棋同意
                else if (move.row == NET_SIGNAL_UNDO_ACK && move.col == NET_SIGNAL_UNDO_ACK)
                {
                    undo_last_moves(2);
                    net_waiting_response = 0;
                    draw_full();
                }
                // 悔棋拒绝
                else if (move.row == NET_SIGNAL_UNDO_NACK && move.col == NET_SIGNAL_UNDO_NACK)
                {
                    MessageBoxW(GetHWnd(), L"对方拒绝了悔棋请求", L"悔棋", MB_OK);
                    net_waiting_response = 0;
                    draw_ui_panel();
                }
                // 和棋请求
                else if (move.row == NET_SIGNAL_DRAW_REQ && move.col == NET_SIGNAL_DRAW_REQ)
                {
                    int choice = MessageBoxW(GetHWnd(),
                        L"对手请求和棋，是否同意？", L"求和", MB_OKCANCEL);
                    if (choice == IDOK)
                    {
                        net_send_move(NET_SIGNAL_DRAW_ACK, NET_SIGNAL_DRAW_ACK);
                        game_running = 0;
                        draw_full();
                        MessageBoxW(GetHWnd(), L"双方和棋！", L"求和", MB_OK);
                        int save_res = MessageBoxW(GetHWnd(), L"是否保存棋谱？", L"保存棋谱", MB_YESNO);
                        if (save_res == IDYES) save_game(3);
                        draw_ui_panel();
                    }
                    else
                    {
                        net_send_move(NET_SIGNAL_DRAW_NACK, NET_SIGNAL_DRAW_NACK);
                    }
                }
                // 和棋同意
                else if (move.row == NET_SIGNAL_DRAW_ACK && move.col == NET_SIGNAL_DRAW_ACK)
                {
                    game_running = 0;
                    net_waiting_response = 0;
                    draw_full();
                    MessageBoxW(GetHWnd(), L"对方同意和棋，本局平局！", L"求和", MB_OK);
                    int save_res = MessageBoxW(GetHWnd(), L"是否保存棋谱？", L"保存棋谱", MB_YESNO);
                    if (save_res == IDYES) save_game(3);
                    draw_ui_panel();
                }
                // 和棋拒绝
                else if (move.row == NET_SIGNAL_DRAW_NACK && move.col == NET_SIGNAL_DRAW_NACK)
                {
                    MessageBoxW(GetHWnd(), L"对方拒绝了和棋请求", L"求和", MB_OK);
                    net_waiting_response = 0;
                    draw_ui_panel();
                }
                // 正常落子
                else if (is_valid_move(move.row, move.col))
                {
                    // 对手回合的落子
                    if (current_player == ((network_role == 1) ? 2 : 1))
                    {
                        board[move.row][move.col] = current_player;
                        audio_play_place();      // 落子音效
                        move_count++;
                        move_history_row[move_count - 1] = move.row;
                        move_history_col[move_count - 1] = move.col;

                        DWORD now = GetTickCount();
                        int elapsed = (int)((now - move_start_tick) / 1000);
                        player_total_time[current_player] -= elapsed;
                        if (player_total_time[current_player] < 0)
                        player_total_time[current_player] = 0;

                        move_start_tick = GetTickCount();//切换落子重置时间

                        if (check_win(move.row, move.col, current_player))
                        {
                            draw_full();
                            wchar_t text[64];
                            swprintf(text, 64, L"游戏结束！对手获胜！共%d步", move_count);
                            MessageBoxW(GetHWnd(), text, L"五子棋", MB_OK);

                            audio_stop_bgm();
                            audio_play_lose();      // 失败音效

                            int save_res = MessageBoxW(GetHWnd(), L"是否保存棋谱？", L"保存棋谱", MB_YESNO);
                            if (save_res == IDYES) save_game(2);

                            game_running = 0;
                            init_board();
                            init_timer();
                            init_hint();
                            draw_ui_panel();
                        }
                        else
                        {
                            current_player = (current_player == 1) ? 2 : 1;
                            draw_full();
                        }
                    }
                }
            }
            else if (ret == -1)
            {
                MessageBoxW(GetHWnd(), L"对手已断开连接", L"网络对局", MB_OK);
                game_running = 0;
                net_disconnect();
                draw_full();
            }
        }

        // 实时更新计时器显示（倒计时效果）
        draw_timer_panel();

        // ---- 回放模式控制 ----
        if (replay_mode == 2)
        {
            static int prev_w = 0, prev_s = 0;
            static int prev_up = 0, prev_down = 0;
            static int prev_space = 0, prev_esc = 0;
            int w = (GetAsyncKeyState('W') & 0x8000) ? 1 : 0;
            int s = (GetAsyncKeyState('S') & 0x8000) ? 1 : 0;
            int up = (GetAsyncKeyState(VK_UP) & 0x8000) ? 1 : 0;
            int down = (GetAsyncKeyState(VK_DOWN) & 0x8000) ? 1 : 0;
            int space = (GetAsyncKeyState(VK_SPACE) & 0x8000) ? 1 : 0;
            int esc = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) ? 1 : 0;

            if ((w || up) && !(prev_w || prev_up))
                replay_step_backward();
            if ((s || down) && !(prev_s || prev_down))
                replay_step_forward();
            if (space && !prev_space)
                replay_toggle_pause();
            if (esc && !prev_esc)
                exit_replay_mode();

            prev_w = w; prev_s = s;
            prev_up = up; prev_down = down;
            prev_space = space; prev_esc = esc;

            // 自动播放
            if (!replay_paused)
            {
                static DWORD replay_auto_tick = 0;
                DWORD now = GetTickCount();
                if (now - replay_auto_tick >= 1000)
                {
                    replay_step_forward();
                    replay_auto_tick = now;
                    if (replay_disp_index >= replay_disp_count)
                        replay_paused = 1;
                }
            }
        }

        Sleep(10);  // 防止忙等导致 CPU 满载
    }

    audio_cleanup();
    net_cleanup();
    closegraph();
    return 0;
}


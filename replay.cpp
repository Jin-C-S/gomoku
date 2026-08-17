#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "replay.h"
#include "gomoku.h"

// ---- 内部数据结构 ----
typedef struct {
    int board_size;
    int move_count;
    int row[MAX_REPLAY_MOVES];
    int col[MAX_REPLAY_MOVES];
    char date[20];
    int result;  // 0=未完成, 1=黑胜, 2=白胜, 3=和棋
} ReplayData;

static ReplayData replay_data;

int replay_mode = 0;
int replay_disp_count = 0;
int replay_disp_index = 0;
int replay_paused = 1;

// 存档/恢复棋盘状态（进入/退出回放时）
static int saved_board[MAX_BOARD][MAX_BOARD];
static int saved_move_count;
static int saved_move_history_row[MAX_MOVES];
static int saved_move_history_col[MAX_MOVES];
static int saved_game_running;
static int saved_game_started;
static int saved_game_mode;

static void save_board_state()
{
    memcpy(saved_board, board, sizeof(board));
    saved_move_count = move_count;
    memcpy(saved_move_history_row, move_history_row, sizeof(move_history_row));
    memcpy(saved_move_history_col, move_history_col, sizeof(move_history_col));
    saved_game_running = game_running;
    saved_game_started = game_started;
    saved_game_mode = game_mode;
}

static void restore_board_state()
{
    memcpy(board, saved_board, sizeof(board));
    move_count = saved_move_count;
    memcpy(move_history_row, saved_move_history_row, sizeof(move_history_row));
    memcpy(move_history_col, saved_move_history_col, sizeof(move_history_col));
    game_running = saved_game_running;
    game_started = saved_game_started;
    game_mode = saved_game_mode;
}

// ---- 文件操作 ----
static void ensure_replay_dir()
{
    CreateDirectoryA("replays", NULL);
}

static void make_filename(char* out, int size)
{
    ensure_replay_dir();
    SYSTEMTIME st;
    GetLocalTime(&st);
    const char* result_str = "未完成";
    if (replay_data.result == 1) result_str = "黑胜";
    else if (replay_data.result == 2) result_str = "白胜";
    else if (replay_data.result == 3) result_str = "和棋";

    for (int seq = 0; seq < 100; seq++)
    {
        if (seq == 0)
            sprintf(out, "replays\\%04d%02d%02d_%02d%02d%02d_%s.gmk",
                    st.wYear, st.wMonth, st.wDay,
                    st.wHour, st.wMinute, st.wSecond, result_str);
        else
            sprintf(out, "replays\\%04d%02d%02d_%02d%02d%02d_%s(%d).gmk",
                    st.wYear, st.wMonth, st.wDay,
                    st.wHour, st.wMinute, st.wSecond, result_str, seq);
        if (GetFileAttributesA(out) == INVALID_FILE_ATTRIBUTES)
            break;
    }
}

// ---- 保存 ----
void save_game(int result)
{
    ensure_replay_dir();

    replay_data.board_size = BOARD_SIZE;
    replay_data.move_count = move_count;
    replay_data.result = result;

    SYSTEMTIME st;
    GetLocalTime(&st);
    sprintf(replay_data.date, "%04d-%02d-%02d %02d:%02d",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    for (int i = 0; i < move_count; i++)
    {
        replay_data.row[i] = move_history_row[i];
        replay_data.col[i] = move_history_col[i];
    }

    char filename[MAX_PATH];
    make_filename(filename, MAX_PATH);

    FILE* fp = fopen(filename, "w");
    if (!fp) return;
    fprintf(fp, "%d\n", replay_data.board_size);
    fprintf(fp, "%s\n", replay_data.date);
    fprintf(fp, "%d\n", replay_data.move_count);
    fprintf(fp, "%d\n", replay_data.result);
    for (int i = 0; i < replay_data.move_count; i++)
        fprintf(fp, "%d %d\n", replay_data.row[i], replay_data.col[i]);
    fclose(fp);

    char msg[128];
    sprintf(msg, "棋谱已保存到 %s", filename);
    MessageBoxA(GetHWnd(), msg, "保存成功", MB_OK);
}

// ---- 加载 ----
static int load_replay(const char* filename)
{
    FILE* fp = fopen(filename, "r");
    if (!fp) return 0;

    if (fscanf(fp, "%d", &replay_data.board_size) != 1) { fclose(fp); return 0; }
    if (fscanf(fp, " %[^\n]", replay_data.date) != 1) { fclose(fp); return 0; }
    if (fscanf(fp, "%d", &replay_data.move_count) != 1) { fclose(fp); return 0; }
    if (replay_data.move_count > MAX_REPLAY_MOVES) replay_data.move_count = MAX_REPLAY_MOVES;
    if (fscanf(fp, "%d", &replay_data.result) != 1) { fclose(fp); return 0; }

    for (int i = 0; i < replay_data.move_count; i++)
    {
        if (fscanf(fp, "%d %d", &replay_data.row[i], &replay_data.col[i]) != 2)
        { fclose(fp); return 0; }
    }
    fclose(fp);

    replay_disp_count = replay_data.move_count;
    replay_disp_index = 0;
    replay_paused = 1;

    // 如果棋盘大小不一致则调整
    if (BOARD_SIZE != replay_data.board_size)
    {
        BOARD_SIZE = replay_data.board_size;
        calc_board_layout();
    }
    return 1;
}

// 获取棋谱文件列表
static int get_replay_list(char names[][64], int max_count)
{
    ensure_replay_dir();
    WIN32_FIND_DATAA ffd;
    char search[MAX_PATH];
    sprintf(search, "replays\\*.gmk");
    HANDLE hFind = FindFirstFileA(search, &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;

    int count = 0;
    do {
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && count < max_count)
            strncpy(names[count], ffd.cFileName, 63);
        count++;
    } while (FindNextFileA(hFind, &ffd) && count < max_count);
    FindClose(hFind);
    return count;
}

static int delete_replay(const char* filename)
{
    char path[MAX_PATH];
    sprintf(path, "replays\\%s", filename);
    return DeleteFileA(path);
}

// ---- 回放绘盘 ----
void replay_draw_board()
{
    for (int r = 0; r < BOARD_SIZE; r++)
        for (int c = 0; c < BOARD_SIZE; c++)
            board[r][c] = 0;

    for (int i = 0; i < replay_disp_index; i++)
    {
        int color = (i % 2 == 0) ? 1 : 2;
        board[replay_data.row[i]][replay_data.col[i]] = color;
    }
    move_count = replay_disp_index;
    draw_full();
}

void replay_step_forward()
{
    if (replay_disp_index < replay_disp_count)
    {
        replay_disp_index++;
        replay_draw_board();
    }
}

void replay_step_backward()
{
    if (replay_disp_index > 0)
    {
        replay_disp_index--;
        replay_draw_board();
    }
}

void replay_toggle_pause()
{
    replay_paused = !replay_paused;
    if (replay_disp_index >= replay_disp_count)
    {
        replay_disp_index = 0;
        replay_draw_board();
    }
}

void exit_replay_mode()
{
    restore_board_state();
    // 恢复棋盘大小可能不同
    if (BOARD_SIZE != saved_board[0][0]) // not perfect but fine
        calc_board_layout();
    replay_mode = 0;
    draw_full();
}

// ---- 回放控制按钮绘制 ----
// 位置：棋盘正下方，提示按钮再下方
static int rc_x()
{
    int board_center = OFFSET_X + MARGIN + (BOARD_SIZE - 1) * CELL_SIZE / 2;
    return board_center - 238;  // 4 个按钮总宽居中
}
static int rc_y()
{
    int board_bottom = OFFSET_Y + MARGIN + (BOARD_SIZE - 1) * CELL_SIZE;
    return board_bottom + 60;
}
#define RC_BTN_W 90
#define RC_BTN_H 34
#define RC_GAP   12

void draw_replay_controls()
{
    int bx = rc_x(), by = rc_y();
    int gap = RC_BTN_W + RC_GAP;

    const char* labels[4] = {"上一步",
                             replay_paused ? "播放" : "暂停",
                             "下一步", "退出"};

    for (int i = 0; i < 4; i++)
    {
        int btn_x = bx + i * gap;
        int is_active = 1;
        if (i == 0 && replay_disp_index <= 0) is_active = 0;
        if (i == 2 && replay_disp_index >= replay_disp_count) is_active = 0;

        setfillcolor(is_active ? RGB(100, 160, 200) : RGB(160, 160, 160));
        setlinecolor(is_active ? RGB(70, 130, 170) : RGB(130, 130, 130));
        setlinestyle(PS_SOLID, 1);
        fillrectangle(btn_x, by, btn_x + RC_BTN_W, by + RC_BTN_H);

        settextcolor(WHITE);
        settextstyle(16, 0, "黑体");
        setbkmode(TRANSPARENT);
        int tw = textwidth(labels[i]);
        outtextxy(btn_x + (RC_BTN_W - tw) / 2, by + (RC_BTN_H - 16) / 2, labels[i]);
    }

    // 步数进度
    char buf[64];
    sprintf(buf, "第 %d / %d 步", replay_disp_index, replay_disp_count);
    settextcolor(RGB(60, 40, 20));
    settextstyle(16, 0, "黑体");
    int board_center = OFFSET_X + MARGIN + (BOARD_SIZE - 1) * CELL_SIZE / 2;
    int tw = textwidth(buf);
    outtextxy(board_center - tw / 2, by - 22, buf);

    // 棋谱信息
    const char* result_str = "未完成";
    if (replay_data.result == 1) result_str = "黑胜";
    else if (replay_data.result == 2) result_str = "白胜";
    else if (replay_data.result == 3) result_str = "和棋";
    sprintf(buf, "%d×%d  %s  %s",
            replay_data.board_size, replay_data.board_size,
            replay_data.date, result_str);
    settextcolor(RGB(120, 100, 80));
    settextstyle(14, 0, "黑体");
    tw = textwidth(buf);
    outtextxy(board_center - tw / 2, by + RC_BTN_H + 8, buf);
}

int handle_replay_control_click(int x, int y)
{
    int bx = rc_x(), by = rc_y();
    int gap = RC_BTN_W + RC_GAP;

    for (int i = 0; i < 4; i++)
    {
        int btn_x = bx + i * gap;
        if (x >= btn_x && x <= btn_x + RC_BTN_W &&
            y >= by && y <= by + RC_BTN_H)
        {
            if (i == 0) { replay_step_backward(); return 1; }
            if (i == 1) { replay_toggle_pause(); return 1; }
            if (i == 2) { replay_step_forward(); return 1; }
            if (i == 3) { exit_replay_mode(); return 1; }
        }
    }
    return 0;
}

// ---- 棋谱库面板 ----
#define RPANEL_W    520
#define RPANEL_H    480
#define RPANEL_X    ((WINDOW_SIZE - RPANEL_W) / 2)
#define RPANEL_Y    ((WINDOW_SIZE - RPANEL_H) / 2)

void draw_replay_panel()
{
    int max_disp;
    // 半透明遮罩
    setfillcolor(0xAA000000);
    solidrectangle(0, 0, WINDOW_SIZE, WINDOW_SIZE);

    // 面板背景
    setfillcolor(RGB(245, 235, 215));
    setlinecolor(RGB(100, 80, 50));
    setlinestyle(PS_SOLID, 2);
    fillrectangle(RPANEL_X, RPANEL_Y, RPANEL_X + RPANEL_W, RPANEL_Y + RPANEL_H);

    settextcolor(RGB(60, 40, 20));
    settextstyle(28, 0, "黑体");
    setbkmode(TRANSPARENT);
    outtextxy(RPANEL_X + 20, RPANEL_Y + 15, "回放存档");

    // 关闭按钮 (X)
    int close_x = RPANEL_X + RPANEL_W - 40;
    int close_y = RPANEL_Y + 12;
    setfillcolor(RGB(200, 100, 100));
    setlinecolor(RGB(160, 60, 60));
    fillrectangle(close_x, close_y, close_x + 28, close_y + 28);
    settextcolor(WHITE);
    settextstyle(20, 0, "黑体");
    outtextxy(close_x + 7, close_y + 4, "×");

    // 获取棋谱列表
    char names[100][64];
    int count = get_replay_list(names, 100);

    int x0 = RPANEL_X + 20;
    int y_cursor = RPANEL_Y + 60;
    int item_h = 36;
    int list_area_h = RPANEL_H - 140;

    if (count == 0)
    {
        settextcolor(RGB(120, 100, 80));
        settextstyle(18, 0, "黑体");
        outtextxy(x0 + 20, y_cursor + 30, "暂无棋谱");
        goto draw_close_btn;
    }

    // 列表头
    settextcolor(RGB(80, 60, 40));
    settextstyle(14, 0, "黑体");
    outtextxy(x0 + 10, y_cursor, "文件名");

    settextstyle(14, 0, "黑体");
    outtextxy(x0 + 280, y_cursor, "步数");
    outtextxy(x0 + 330, y_cursor, "结果");
    y_cursor += 5;

    // 分割线
    setlinecolor(RGB(180, 160, 130));
    line(RPANEL_X + 15, y_cursor, RPANEL_X + RPANEL_W - 15, y_cursor);
    y_cursor += 10;

    max_disp = list_area_h / item_h;
    if (max_disp > count) max_disp = count;

    for (int i = 0; i < max_disp; i++)
    {
        int iy = y_cursor + i * item_h;

        // 交替背景行
        if (i % 2 == 0)
        {
            setfillcolor(RGB(235, 225, 205));
            solidrectangle(RPANEL_X + 15, iy, RPANEL_X + RPANEL_W - 15, iy + item_h);
        }

        // 尝试解析文件名获取信息
        char fname[32];
        strncpy(fname, names[i], 25);
        fname[25] = 0;
        // 去掉 .gmk 后缀
        char* dot = strstr(fname, ".gmk");
        if (dot) *dot = 0;

        settextcolor(RGB(60, 40, 20));
        settextstyle(13, 0, "黑体");
        setbkmode(TRANSPARENT);
        outtextxy(x0 + 10, iy + 10, fname);

        // 从文件名解析 步数 和 结果
        char filepath[MAX_PATH];
        sprintf(filepath, "replays\\%s", names[i]);
        FILE* fp = fopen(filepath, "r");
        if (fp)
        {
            int bs, mc, res;
            char dt[20];
            fscanf(fp, "%d %[^\n] %d %d", &bs, dt, &mc, &res);
            fclose(fp);
            char buf[16];
            sprintf(buf, "%d", mc);
            outtextxy(x0 + 280, iy + 10, buf);
            const char* rs = "未完成";
            if (res == 1) rs = "黑胜";
            else if (res == 2) rs = "白胜";
            else if (res == 3) rs = "和棋";
            outtextxy(x0 + 330, iy + 10, rs);
        }

        // 回放按钮
        int bx = RPANEL_X + RPANEL_W - 130;
        setfillcolor(RGB(80, 150, 80));
        setlinecolor(RGB(50, 120, 50));
        fillrectangle(bx, iy + 4, bx + 44, iy + item_h - 4);
        settextcolor(WHITE);
        settextstyle(13, 0, "黑体");
        outtextxy(bx + 5, iy + 9, "回放");

        // 删除按钮
        bx += 50;
        setfillcolor(RGB(180, 100, 100));
        setlinecolor(RGB(150, 70, 70));
        fillrectangle(bx, iy + 4, bx + 44, iy + item_h - 4);
        settextcolor(WHITE);
        outtextxy(bx + 5, iy + 9, "删除");
    }

draw_close_btn:
    char buf2[32];
    sprintf(buf2, "共 %d 个棋谱", count);
    settextcolor(RGB(120, 100, 80));
    settextstyle(14, 0, "黑体");
    outtextxy(RPANEL_X + 20, RPANEL_Y + RPANEL_H - 90, buf2);

    // 底部关闭
    int btn_close_x = RPANEL_X + (RPANEL_W - 120) / 2;
    int btn_close_y = RPANEL_Y + RPANEL_H - 48;
    setfillcolor(RGB(160, 130, 100));
    setlinecolor(RGB(130, 100, 70));
    fillrectangle(btn_close_x, btn_close_y, btn_close_x + 120, btn_close_y + 36);
    settextcolor(WHITE);
    settextstyle(18, 0, "黑体");
    int tw = textwidth("关闭");
    outtextxy(btn_close_x + (120 - tw) / 2, btn_close_y + 9, "关闭");
}

int handle_replay_click(int x, int y)
{
    // 关闭按钮 (X)
    int close_x = RPANEL_X + RPANEL_W - 40;
    int close_y = RPANEL_Y + 12;
    if (x >= close_x && x <= close_x + 28 && y >= close_y && y <= close_y + 28)
    {
        replay_mode = 0;
        draw_full();
        return 1;
    }

    // 底部关闭按钮
    int btn_close_x = RPANEL_X + (RPANEL_W - 120) / 2;
    int btn_close_y = RPANEL_Y + RPANEL_H - 48;
    if (x >= btn_close_x && x <= btn_close_x + 120 &&
        y >= btn_close_y && y <= btn_close_y + 36)
    {
        replay_mode = 0;
        draw_full();
        return 1;
    }

    // 点击遮罩外部
    if (x < RPANEL_X || x > RPANEL_X + RPANEL_W ||
        y < RPANEL_Y || y > RPANEL_Y + RPANEL_H)
    {
        replay_mode = 0;
        draw_full();
        return 1;
    }

    // 获取列表以处理点击
    char names[100][64];
    int count = get_replay_list(names, 100);
    if (count == 0) return 1;

    int x0 = RPANEL_X + 20;
    int y_cursor = RPANEL_Y + 60 + 15;
    int item_h = 36;
    int list_area_h = RPANEL_H - 140;
    int max_disp = list_area_h / item_h;
    if (max_disp > count) max_disp = count;

    for (int i = 0; i < max_disp; i++)
    {
        int iy = y_cursor + i * item_h;

        // 回放按钮
        int bx = RPANEL_X + RPANEL_W - 130;
        if (x >= bx && x <= bx + 44 && y >= iy + 4 && y <= iy + item_h - 4)
        {
            char path[MAX_PATH];
            sprintf(path, "replays\\%s", names[i]);
            if (load_replay(path))
            {
                save_board_state();       // 保存当前棋盘
                replay_mode = 2;          // 进入回放模式
                replay_paused = 1;
                replay_disp_index = 0;
                replay_draw_board();
            }
            return 1;
        }

        // 删除按钮
        bx += 50;
        if (x >= bx && x <= bx + 44 && y >= iy + 4 && y <= iy + item_h - 4)
        {
            char msg[128];
            sprintf(msg, "确定删除棋谱 \"%s\"？", names[i]);
            int choice = MessageBoxA(GetHWnd(), msg, "删除棋谱", MB_OKCANCEL);
            if (choice == IDOK) { delete_replay(names[i]); draw_full(); }
            return 1;
        }
    }

    return 1;
}

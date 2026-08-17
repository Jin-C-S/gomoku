#ifndef GOMOKU_H
#define GOMOKU_H

#include <easyx.h>
#include <graphics.h>

// 棋盘与窗口常量
#define MAX_BOARD   15      // 最大棋盘尺寸（数组固定大小）
#define MARGIN      20
#define OFFSET_X    240     // 棋盘左边距（留空间给UI）
#define WINDOW_SIZE 864

// 按钮区域
#define BTN_X       20
#define BTN_W       200
#define BTN_H       40

// 运行时可调整的参数
extern int BOARD_SIZE;
extern int CELL_SIZE;
extern int BOARD_PX;
extern int OFFSET_Y;

// 棋盘状态: 0=空, 1=黑子, 2=白子
extern int board[MAX_BOARD][MAX_BOARD];
extern int current_player;  // 1=黑棋, 2=白棋
extern int move_count;
extern int game_mode;       // 0=人人对弈, 1=人机对弈, 2=网络对弈
#define GAME_MODE_NETWORK 2
extern int game_running;    // 0=未开始, 1=进行中
extern int game_started;    // 0=未开始过游戏（选单阶段）, 1=已开始过游戏
extern int settings_open;   // 0=关闭设置, 1=打开设置
extern IMAGE bg_img;

// 落子提示
extern int hint_row;
extern int hint_col;
extern int hint_remain;

// 历史记录（悔棋用）
#define MAX_MOVES 225
extern int move_history_row[MAX_MOVES];
extern int move_history_col[MAX_MOVES];

// 函数声明
void calc_board_layout();
void init_board();
void draw_piece(int row, int col, int color);
int count_in_direction(int row, int col, int dr, int dc, int color);
int check_win(int row, int col, int color);
int is_valid_move(int row, int col);
void analyze_line(int row, int col, int dr, int dc, int color,
                  int* count, int* open_ends);
int is_forbidden(int row, int col, int color);
void draw_board_grid();
void draw_full();
void draw_ui_panel();
int handle_button_click(int x, int y);

#endif // GOMOKU_H

#ifndef REPLAY_H
#define REPLAY_H

#define MAX_REPLAY_MOVES 225

extern int replay_mode;          // 0=关闭, 1=浏览棋谱库, 2=回放中
extern int replay_disp_count;    // 回放总步数
extern int replay_disp_index;    // 当前显示到第几步 (0=空棋盘)
extern int replay_paused;        // 0=自动播放, 1=暂停

void save_game(int result);
void draw_replay_panel();
int handle_replay_click(int x, int y);
void replay_draw_board();
void replay_step_forward();
void replay_step_backward();
void replay_toggle_pause();
void draw_replay_controls();
int handle_replay_control_click(int x, int y);
void exit_replay_mode();

#endif

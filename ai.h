#ifndef AI_H
#define AI_H

// AI 决策：返回最佳落子位置
void ai_decide(int* best_row, int* best_col);

// AI 决策（困难难度）：alpha-beta 剪枝搜索，返回最佳落子位置
void ai_decide_alpha(int* best_row, int* best_col);

// 根据设置中的 AI 难度选择决策函数
void ai_get_move(int* best_row, int* best_col);

#endif // AI_H

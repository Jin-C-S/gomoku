#include <windows.h>
#include "gomoku.h"
#include "ai.h"
#include "settings.h"

// ---- 评分表 ----
#define SCORE_FIVE    100000
#define SCORE_LIVE4   10000
#define SCORE_RUSH4   5000
#define SCORE_LIVE3   3000
#define SCORE_SLEEP3  1000
#define SCORE_LIVE2   500
#define SCORE_SLEEP2  100
#define SCORE_LIVE1   50

// 评估某个方向上的棋型得分
// 假设在 (row,col) 落子 color，沿 (dr,dc) 方向分析
static int eval_dir(int row, int col, int dr, int dc, int color)
{
    int count = 1;  // 当前假设的落子

    // 正方向统计
    int r = row + dr, c = col + dc;
    while (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE
           && board[r][c] == color)
    {
        count++;
        r += dr;
        c += dc;
    }
    int fwd_open = (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE
                    && board[r][c] == 0);

    // 反方向统计
    r = row - dr;
    c = col - dc;
    while (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE
           && board[r][c] == color)
    {
        count++;
        r -= dr;
        c -= dc;
    }
    int bwd_open = (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE
                    && board[r][c] == 0);

    int open_ends = (fwd_open ? 1 : 0) + (bwd_open ? 1 : 0);

    // 根据棋型评分
    if (count >= 5) return SCORE_FIVE;
    if (count == 4)
    {
        if (open_ends == 2) return SCORE_LIVE4;
        if (open_ends == 1) return SCORE_RUSH4;
        return 0;
    }
    if (count == 3)
    {
        if (open_ends == 2) return SCORE_LIVE3;
        if (open_ends == 1) return SCORE_SLEEP3;
        return 0;
    }
    if (count == 2)
    {
        if (open_ends == 2) return SCORE_LIVE2;
        if (open_ends == 1) return SCORE_SLEEP2;
        return 0;
    }
    if (count == 1)
    {
        if (open_ends == 2) return SCORE_LIVE1;
        if (open_ends == 1) return 10;   // 一端开放的单子，价值极低
        return 0;
    }
    return 0;
}

// 评估在 (row,col) 落 color 棋的总得分（4个方向合计）
static int eval_position(int row, int col, int color)
{
    int dirs[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};
    int total = 0;
    for (int d = 0; d < 4; d++)
        total += eval_dir(row, col, dirs[d][0], dirs[d][1], color);
    return total;
}

// AI 决策：遍历所有空位，综合攻击分与防守分，取最高分位置
// AI 执白（player=2），玩家执黑（player=1）
void ai_decide(int* best_row, int* best_col)
{
    *best_row = -1;
    *best_col = -1;

    // 首步落天元（棋盘中心）
    int center = BOARD_SIZE / 2;
    if (board[center][center] == 0)
    {
        *best_row = center;
        *best_col = center;
        return;
    }

    int best_score = -1;

    for (int r = 0; r < BOARD_SIZE; r++)
    {
        for (int c = 0; c < BOARD_SIZE; c++)
        {
            if (board[r][c] != 0) continue;

            // AI 进攻分（白棋）
            int attack = eval_position(r, c, 2);
            // 防守分（模拟玩家黑棋落在此处的价值）
            int defense = eval_position(r, c, 1);

            // 综合评分：防守权重略高（1.1倍）
            int score = defense * 11 / 10 + attack;

            if (score > best_score)
            {
                best_score = score;
                *best_row = r;
                *best_col = c;
            }
        }
    }
}

// ============================================================
// 困难难度 AI：alpha-beta 剪枝（negamax + 迭代加深 + 限时）
//
// 为兼顾复杂度与响应时间做了三层控制：
//   1. 候选点裁剪 —— 只考虑已有棋子 2 格范围内的空位（开局约 20~50 个）
//   2. 走法排序   —— 按"攻防最大值"降序排列，大幅提升剪枝效率
//   3. 限时       —— 迭代加深逐层深入，超时即用上一层完整搜索结果
// ============================================================

#define AB_MAX_DEPTH    4        // 最大搜索深度
#define AB_TIME_BUDGET  250      // 每步思考时间预算（毫秒）
#define AB_INF          100000000

static DWORD   ab_deadline;      // 搜索截止时间戳
static int     ab_abort;         // 超时中止标记
static long    ab_nodes;         // 已评估节点数（定期检查时间用）

// 棋盘上是否已有棋子
static int board_has_stone()
{
    for (int r = 0; r < BOARD_SIZE; r++)
        for (int c = 0; c < BOARD_SIZE; c++)
            if (board[r][c] != 0) return 1;
    return 0;
}

// (r,c) 的 2 格范围内是否存在棋子（候选点 / 叶评估都用它过滤）
static int is_near_stone(int r, int c)
{
    for (int dr = -2; dr <= 2; dr++)
        for (int dc = -2; dc <= 2; dc++)
        {
            int nr = r + dr, nc = c + dc;
            if (nr >= 0 && nr < BOARD_SIZE && nc >= 0 && nc < BOARD_SIZE
                && board[nr][nc] != 0)
                return 1;
        }
    return 0;
}

// 生成候选点：所有已有棋子 2 格范围内的空位
static int gen_moves(int moves[][2])
{
    int n = 0;
    for (int r = 0; r < BOARD_SIZE; r++)
    {
        for (int c = 0; c < BOARD_SIZE; c++)
        {
            if (board[r][c] != 0) continue;
            if (!is_near_stone(r, c)) continue;
            moves[n][0] = r;
            moves[n][1] = c;
            n++;
        }
    }
    return n;
}

// 把候选点按"在该处落子对当前方的价值"降序排序，提升 alpha-beta 剪枝效率
// 价值取 max(进攻分, 防守分)：既能优先走出自己的杀招，也不会漏掉堵住对方杀招的点
// 先一次性算好各点价值再排序，避免在插入排序中重复计算
static void order_moves(int moves[][2], int n, int color)
{
    int opp = (color == 1) ? 2 : 1;
    int val[MAX_MOVES];
    for (int i = 0; i < n; i++)
    {
        int av = eval_position(moves[i][0], moves[i][1], color);
        int dv = eval_position(moves[i][0], moves[i][1], opp);
        val[i] = av > dv ? av : dv;
    }
    // 插入排序（n 通常 <60）
    for (int i = 1; i < n; i++)
    {
        int v = val[i];
        int r = moves[i][0], c = moves[i][1];
        int j = i - 1;
        while (j >= 0 && val[j] < v)
        {
            val[j + 1] = val[j];
            moves[j + 1][0] = moves[j][0];
            moves[j + 1][1] = moves[j][1];
            j--;
        }
        val[j + 1] = v;
        moves[j + 1][0] = r;
        moves[j + 1][1] = c;
    }
}

// 叶子局面打分：返回"当前行动方 color"视角的分值（正=对 color 有利）
// 取 color 方最强两个威胁点之和，减去对方最强两个威胁点之和，
// 从而体现双三/双四等复合威胁的价值
static int evaluate_board(int color)
{
    int opp = (color == 1) ? 2 : 1;
    int my1 = 0, my2 = 0, op1 = 0, op2 = 0;

    for (int r = 0; r < BOARD_SIZE; r++)
    {
        for (int c = 0; c < BOARD_SIZE; c++)
        {
            if (board[r][c] != 0) continue;
            if (!is_near_stone(r, c)) continue;   // 远处的空格不会形成有效威胁

            int mv = eval_position(r, c, color);
            int ov = eval_position(r, c, opp);
            if (mv > my1) { my2 = my1; my1 = mv; }
            else if (mv > my2) my2 = mv;
            if (ov > op1) { op2 = op1; op1 = ov; }
            else if (ov > op2) op2 = ov;
        }
    }
    return (my1 + my2) - (op1 + op2);
}

// negamax + alpha-beta 递归搜索
// 参数：剩余深度、alpha/beta 界、当前行动方颜色
// 返回当前行动方视角的最佳分值
static int negamax(int depth, int alpha, int beta, int color)
{
    // 定期检查超时（每 1024 个节点一次，开销极小）
    if ((++ab_nodes & 1023) == 0 && GetTickCount() > ab_deadline)
    {
        ab_abort = 1;
        return 0;
    }

    if (depth == 0)
        return evaluate_board(color);

    int moves[MAX_MOVES][2];
    int n = gen_moves(moves);
    if (n == 0) return 0;   // 无空位，平局

    order_moves(moves, n, color);

    int best = -AB_INF;
    for (int i = 0; i < n; i++)
    {
        int r = moves[i][0], c = moves[i][1];
        board[r][c] = color;

        int val;
        if (check_win(r, c, color))
            val = SCORE_FIVE + depth;   // 本层即可获胜：越早赢价值越高
        else
            val = -negamax(depth - 1, -beta, -alpha, (color == 1) ? 2 : 1);

        board[r][c] = 0;    // 撤销落子

        if (val > best) best = val;
        if (best > alpha) alpha = best;
        if (alpha >= beta) break;   // β 剪枝：后续分支已不可能影响决策

        if (ab_abort) break;
    }
    return best;
}

// AI 决策（困难难度）：alpha-beta 剪枝
void ai_decide_alpha(int* best_row, int* best_col)
{
    *best_row = -1;
    *best_col = -1;

    // 空棋盘：首步落天元（棋盘中心）
    if (!board_has_stone())
    {
        *best_row = BOARD_SIZE / 2;
        *best_col = BOARD_SIZE / 2;
        return;
    }

    int moves[MAX_MOVES][2];
    int n = gen_moves(moves);
    if (n == 0) return;
    if (n == 1)
    {
        *best_row = moves[0][0];
        *best_col = moves[0][1];
        return;
    }

    // 快速路径：AI（白）能立即五连 → 直接落子，不再搜索
    for (int i = 0; i < n; i++)
    {
        int r = moves[i][0], c = moves[i][1];
        board[r][c] = 2;
        int win = check_win(r, c, 2);
        board[r][c] = 0;
        if (win)
        {
            *best_row = r;
            *best_col = c;
            return;
        }
    }

    order_moves(moves, n, 2);

    // 迭代加深：从深度 1 开始逐层完整搜索
    // 超时后使用上一层已完成的搜索结果，保证任何情况下都有合法落子
    int best_r = moves[0][0], best_c = moves[0][1];
    ab_deadline = GetTickCount() + AB_TIME_BUDGET;
    ab_abort = 0;
    ab_nodes = 0;

    for (int depth = 1; depth <= AB_MAX_DEPTH; depth++)
    {
        if (GetTickCount() > ab_deadline) break;   // 时间到，用上一层结果

        int alpha = -AB_INF, beta = AB_INF;
        int found = 0;

        for (int i = 0; i < n; i++)
        {
            int r = moves[i][0], c = moves[i][1];
            board[r][c] = 2;

            int val;
            if (check_win(r, c, 2))
                val = SCORE_FIVE + depth;
            else
                val = -negamax(depth - 1, -beta, -alpha, 1);

            board[r][c] = 0;

            if (val > alpha)
            {
                alpha = val;
                best_r = r;
                best_c = c;
                found = 1;
            }
            if (ab_abort) break;   // 本层搜索被超时打断，丢弃，保留上一层结果
        }

        if (found)
        {
            *best_row = best_r;
            *best_col = best_c;
        }
        if (ab_abort) break;
    }
}

// 根据设置中的 AI 难度选择决策函数
void ai_get_move(int* best_row, int* best_col)
{
    if (game_settings.ai_difficulty == AI_DIFF_HARD)
        ai_decide_alpha(best_row, best_col);
    else
        ai_decide(best_row, best_col);
}

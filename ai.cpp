#include <windows.h>
#include "gomoku.h"
#include "ai.h"
#include "settings.h"

// ---- 棋型枚举与评分表 ----
// 单方向棋型按强弱递增排序；黑棋 长连/双三/双四 为禁手，用 SHAPE_FORBIDDEN 表示（不构成威胁）
enum ShapeCode {
    SHAPE_NONE = 0,
    SHAPE_LIVE1, SHAPE_SLEEP2, SHAPE_LIVE2, SHAPE_SLEEP3,
    SHAPE_JUMP3, SHAPE_LIVE3, SHAPE_JUMP4, SHAPE_RUSH4, SHAPE_LIVE4,
    SHAPE_FIVE, SHAPE_FORBIDDEN
};

#define SCORE_FIVE         10000000  // 成五
#define SCORE_DOUBLE_FOUR   8000000  // 双四（必胜）
#define SCORE_FOUR_THREE    6000000  // 四三（必胜）
#define SCORE_DOUBLE_THREE  4000000  // 双活三（强杀；黑棋=禁手）
#define SCORE_LIVE4         3000000  // 活四
#define SCORE_RUSH4          200000  // 冲四
#define SCORE_JUMP4          150000  // 跳四
#define SCORE_LIVE3           60000  // 活三
#define SCORE_JUMP3           40000  // 跳三
#define SCORE_SLEEP3          10000  // 眠三
#define SCORE_LIVE2            2000  // 活二
#define SCORE_SLEEP2            500  // 眠二
#define SCORE_LIVE1              80  // 活一
#define SCORE_FORBIDDEN    -8000000  // 禁手（极大负，不作为威胁）

// 必胜级阈值：活四及以上视为"即将成五"的必杀级威胁
#define SCORE_MATE       SCORE_LIVE4
#define MATE_BONUS        5000000

// 读取窗口内某格：0=空, 1=己方, 2=对方或出界(被堵)
static int cell_at(int row, int col, int dr, int dc, int k, int color)
{
    int r = row + dr * k, c = col + dc * k;
    if (r < 0 || r >= BOARD_SIZE || c < 0 || c >= BOARD_SIZE) return 2;
    int v = board[r][c];
    if (v == 0) return 0;
    if (v == color) return 1;
    return 2;
}

// 单方向棋型判定：以 (row,col) 落子 color 后，沿 (dr,dc) 方向识别棋型。
// 用 9 格窗口识别"连续"与"跳型"(X_XXX / XX_XX / X_XX 等)，并处理黑棋长连禁手。
static ShapeCode classify_dir(int row, int col, int dr, int dc, int color)
{
    int ln[9];
    for (int k = -4; k <= 4; k++) ln[k + 4] = cell_at(row, col, dr, dc, k, color);
    ln[4] = 1;   // 窗口中心恒为"假设落下的己方子"（评估空位时 board 尚未放置）
    const int c = 4;

    int lo = c, hi = c;
    while (lo - 1 >= 0 && ln[lo - 1] == 1) lo--;
    while (hi + 1 < 9 && ln[hi + 1] == 1) hi++;
    int cont = hi - lo + 1;
    int openL = (lo - 1 >= 0 && ln[lo - 1] == 0);
    int openR = (hi + 1 < 9 && ln[hi + 1] == 0);
    int ends = (openL ? 1 : 0) + (openR ? 1 : 0);

    // 基础型：连续长度 + 两端开放
    ShapeCode base;
    if (cont >= 6)          base = SHAPE_FORBIDDEN;                          // 长连(禁手)
    else if (cont == 5)     base = SHAPE_FIVE;
    else if (cont == 4)     base = (ends == 2) ? SHAPE_LIVE4 : (ends == 1) ? SHAPE_RUSH4 : SHAPE_NONE;
    else if (cont == 3)     base = (ends == 2) ? SHAPE_LIVE3 : (ends == 1) ? SHAPE_SLEEP3 : SHAPE_NONE;
    else if (cont == 2)     base = (ends == 2) ? SHAPE_LIVE2 : (ends == 1) ? SHAPE_SLEEP2 : SHAPE_NONE;
    else if (cont == 1)     base = (ends == 2) ? SHAPE_LIVE1 : SHAPE_NONE;
    else                    base = SHAPE_NONE;

    // 跳型：连续段之外隔 1 个空位后，再连续数己方子
    // 要求连续段 >=2（落点必须至少链接到一个相邻子），避免孤立点被误判成跳型
    auto beyond = [&](int idx, int step) -> int {
        int nxt = idx + step;
        if (idx < 0 || idx >= 9 || ln[idx] != 0) return 0;   // 缺口必须是空
        if (nxt < 0 || nxt >= 9 || ln[nxt] != 1) return 0;   // 缺口后必须直接是己方子
        int cnt = 0, p = nxt;
        while (p >= 0 && p < 9 && ln[p] == 1) { cnt++; p += step; }
        return cnt;
    };
    int left_stone  = beyond(lo - 1, -1);
    int right_stone = beyond(hi + 1, 1);

    if (base != SHAPE_FORBIDDEN && cont >= 2 && (left_stone || right_stone))
    {
        int cont_plus = cont + left_stone + right_stone;
        if (cont_plus >= 5) cont_plus = 4;   // 缺口不构成真正五连，封顶按四
        if (cont_plus == 4 && base < SHAPE_JUMP4) return SHAPE_JUMP4;
        if (cont_plus == 3 && base < SHAPE_JUMP3) return SHAPE_JUMP3;
    }
    return base;
}

// 棋型 → 分数
static int shape_score(ShapeCode s)
{
    switch (s)
    {
    case SHAPE_FIVE:   return SCORE_FIVE;
    case SHAPE_LIVE4:  return SCORE_LIVE4;
    case SHAPE_RUSH4:  return SCORE_RUSH4;
    case SHAPE_JUMP4:  return SCORE_JUMP4;
    case SHAPE_LIVE3:  return SCORE_LIVE3;
    case SHAPE_JUMP3:  return SCORE_JUMP3;
    case SHAPE_SLEEP3: return SCORE_SLEEP3;
    case SHAPE_LIVE2:  return SCORE_LIVE2;
    case SHAPE_SLEEP2: return SCORE_SLEEP2;
    case SHAPE_LIVE1:  return SCORE_LIVE1;
    default:           return 0;
    }
}

// 该方向是否构成"四"威胁
static int dir_is_four(ShapeCode s)
{
    return (s == SHAPE_LIVE4 || s == SHAPE_RUSH4 || s == SHAPE_JUMP4);
}

// 该方向是否构成"活三"
static int dir_is_live_three(ShapeCode s)
{
    return (s == SHAPE_LIVE3);
}

// 评估在 (row,col) 落 color 棋的价值（组合质变版）
// 先对 4 个方向定性，再按"威胁组合"加分：双四/四三/双三 远高于单方向得分之和；
// 黑棋的 长连/双四/双三 在开启禁手规则时视为禁手（最大负，不构成威胁）。
static int eval_position(int row, int col, int color)
{
    static const int dirs[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};

    ShapeCode d[4];
    int overline = 0;
    for (int i = 0; i < 4; i++)
    {
        d[i] = classify_dir(row, col, dirs[i][0], dirs[i][1], color);
        if (d[i] == SHAPE_FORBIDDEN) overline = 1;
    }

    int n_four = 0, n_live3 = 0, base = 0;
    for (int i = 0; i < 4; i++)
    {
        ShapeCode s = d[i];
        if (s == SHAPE_FORBIDDEN) { base += SCORE_FORBIDDEN; continue; }
        base += shape_score(s);
        if (dir_is_four(s)) n_four++;
        if (dir_is_live_three(s)) n_live3++;
    }

    // 黑棋禁手（仅开启禁手规则时生效）
    if (color == 1 && game_settings.forbidden_enabled)
    {
        if (overline)     return SCORE_FORBIDDEN;  // 长连禁手
        if (n_four >= 2)  return SCORE_FORBIDDEN;  // 四四禁手
        if (n_live3 >= 2) return SCORE_FORBIDDEN;  // 三三禁手
    }

    // 组合质变：介于"单方向分"与"成五"之间
    if (n_four >= 2)                 return SCORE_DOUBLE_FOUR;
    if (n_four >= 1 && n_live3 >= 1)  return SCORE_FOUR_THREE;
    if (n_live3 >= 2)                 return SCORE_DOUBLE_THREE;
    return base;
}

// 该落点是否会对 color 方构成"四"威胁（用于快速防守判断）
static int pos_has_four(int row, int col, int color)
{
    static const int dirs[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};
    for (int i = 0; i < 4; i++)
        if (dir_is_four(classify_dir(row, col, dirs[i][0], dirs[i][1], color)))
            return 1;
    return 0;
}

// 该落点是否会对 color 方构成"活三"威胁
static int pos_has_live_three(int row, int col, int color)
{
    static const int dirs[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};
    for (int i = 0; i < 4; i++)
        if (dir_is_live_three(classify_dir(row, col, dirs[i][0], dirs[i][1], color)))
            return 1;
    return 0;
}

// 落子是否构成"强制获胜"组合（以攻代守的判断依据）：
// 任意方向成"活四"，或同时构成 双四 / 四三（这些是对方挡不完的必杀威胁）。
// 黑棋的 四四/三三 是禁手（开启禁手规则时），不作为强制获胜组合。
static int pos_force_win(int row, int col, int color)
{
    static const int dirs[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};
    int n_four = 0, n_live3 = 0, live4 = 0;
    for (int i = 0; i < 4; i++)
    {
        ShapeCode s = classify_dir(row, col, dirs[i][0], dirs[i][1], color);
        if (s == SHAPE_FORBIDDEN) continue;   // 禁手（黑棋长连）不算获胜
        if (dir_is_four(s)) { n_four++; if (s == SHAPE_LIVE4) live4 = 1; }
        if (dir_is_live_three(s)) n_live3++;
    }
    if (color == 1 && game_settings.forbidden_enabled)
    {
        if (n_four >= 2)  return 0;   // 四四禁手
        if (n_live3 >= 2) return 0;   // 三三禁手
    }
    if (live4)                        return 1;  // 活四：对方挡不完
    if (n_four >= 2)                  return 1;  // 双四：必胜
    if (n_four >= 1 && n_live3 >= 1)   return 1;  // 四三：必胜
    return 0;
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

            // 综合评分：防守权重略高（1.2倍）
            int score = defense * 12 / 10 + attack;

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
// 困难难度 AI：alpha-beta 剪枝（negamax + 迭代加深）
//
// 为兼顾效率与强弱做了控制：
//   1. 候选点裁剪 —— 只考虑已有棋子 2 格范围内的空位（开局约 20~50 个）
//   2. 走法排序   —— 按"攻防最大值"降序排列，大幅提升剪枝效率
//   3. 深度控制   —— 迭代加深逐层深入，始终搜索到最大深度（超时中止已注释）
// ============================================================

#ifndef AB_MAX_DEPTH
#define AB_MAX_DEPTH    4        // 最大搜索深度（可在编译期 -D 覆盖，便于调参/对局模拟提速）
#endif
#define AB_TIME_BUDGET  250      // 每步思考时间预算（毫秒）
#define AB_INF          100000000

// 超时中止控制已注释：改为始终搜索到最大深度，避免超时中止把被打断层的结果当作最终着法
// static DWORD   ab_deadline;      // 搜索截止时间戳
// static int     ab_abort;         // 超时中止标记
// static long    ab_nodes;         // 已评估节点数（定期检查时间用）

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

// 把候选点按"在该处落子的综合价值"降序排序，提升 alpha-beta 剪枝效率
// 价值取 进攻分+防守分：既能优先走出自己的杀招，也兼顾堵对方杀招的点，
// 让"一子两用"（既攻又守）的点排到最前，配合迭代加深尽早锁定强手。
static void order_moves(int moves[][2], int n, int color)
{
    int opp = (color == 1) ? 2 : 1;
    int val[MAX_MOVES];
    for (int i = 0; i < n; i++)
    {
        int av = eval_position(moves[i][0], moves[i][1], color);
        int dv = eval_position(moves[i][0], moves[i][1], opp);
        val[i] = av + dv;
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
// 用组合质变后的落点价值累加，并单独统计"必胜级"威胁（活四/双三/四三/双四）。
// 必胜级威胁数主导分值，避免"多个普通威胁"被误判强于"一次必胜"。
static int evaluate_board(int color)
{
    int opp = (color == 1) ? 2 : 1;
    int my_total = 0, op_total = 0;
    int my_mate = 0, op_mate = 0;

    for (int r = 0; r < BOARD_SIZE; r++)
    {
        for (int c = 0; c < BOARD_SIZE; c++)
        {
            if (board[r][c] != 0) continue;
            if (!is_near_stone(r, c)) continue;   // 远处的空格不会形成有效威胁

            int mv = eval_position(r, c, color);
            int ov = eval_position(r, c, opp);

            if (mv >= SCORE_MATE) my_mate++;
            else                  my_total += mv;

            if (ov >= SCORE_MATE) op_mate++;
            else                  op_total += ov;
        }
    }

    int my = my_mate * MATE_BONUS + my_total;
    int op = op_mate * MATE_BONUS + op_total;
    return my - op;
}

// negamax + alpha-beta 递归搜索
// 参数：剩余深度、alpha/beta 界、当前行动方颜色
// 返回当前行动方视角的最佳分值
static int negamax(int depth, int alpha, int beta, int color)
{
    // 定期检查超时（每 256 个节点一次，避免严重超时）——已注释，改用固定深度搜索
    // if ((++ab_nodes & 255) == 0 && GetTickCount() > ab_deadline)
    // {
    //     ab_abort = 1;
    //     return 0;
    // }

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

        // if (ab_abort) break;   // 超时中止已注释
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

    // 快速路径 1：堵住黑棋"立即五连"的成五点（最高优先级，不能被"四威胁"抢先打断）
    for (int i = 0; i < n; i++)
    {
        int r = moves[i][0], c = moves[i][1];
        board[r][c] = 1;  // 检查黑棋(玩家)
        int win = check_win(r, c, 1);
        board[r][c] = 0;
        if (win) { *best_row = r; *best_col = c; return; }
    }

    // 快速路径 2：以攻代守——黑棋尚无"立即成五"点时，若白棋某手能形成
    // 强制获胜组合（活四/双四/四三），优先自己取胜，而不是单纯堵防。
    for (int i = 0; i < n; i++)
    {
        int r = moves[i][0], c = moves[i][1];
        if (pos_force_win(r, c, 2))
        {
            *best_row = r;
            *best_col = c;
            return;
        }
    }

    // 快速路径 3：白棋没有强制杀时才防守——若黑棋存在"可形成四"的威胁点，
    // 直接堵住其四扩展点（保持原防守优先语义；首个命中即返回）。
    for (int i = 0; i < n; i++)
    {
        int r = moves[i][0], c = moves[i][1];
        board[r][c] = 1;
        int has_four = pos_has_four(r, c, 1);
        board[r][c] = 0;
        if (has_four) { *best_row = r; *best_col = c; return; }
    }

    // 其余（活三应对、布子等）交由排序与搜索权衡处理：order_moves 现按攻防
    // 合计排序，组合质变评分会让威胁点排到前面，搜索深度内自行权衡攻守。

    order_moves(moves, n, 2);

    // 迭代加深：从深度 1 开始逐层完整搜索
    // （超时中止已注释，改为始终搜索到最大深度）
    int best_r = moves[0][0], best_c = moves[0][1];
    // ab_deadline = GetTickCount() + AB_TIME_BUDGET;
    // ab_abort = 0;
    // ab_nodes = 0;

    // 动态调整搜索深度：候选点少时可搜索更深
    int max_depth = AB_MAX_DEPTH;
    if (n < 10) max_depth = 6;
    else if (n < 20) max_depth = 5;

    for (int depth = 1; depth <= max_depth; depth++)
    {
        // if (GetTickCount() > ab_deadline) break;   // 时间到，用上一层结果（已注释）

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

            // 虽然根节点 beta = +INF，但为规范起见仍检查剪枝
            if (alpha >= beta) break;

            // if (ab_abort) break;   // 本层搜索被超时打断（已注释）
        }

        if (found)
        {
            *best_row = best_r;
            *best_col = best_c;
        }
        // if (ab_abort) break;   // 超时中止已注释
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

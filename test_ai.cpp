// 测试：重放棋谱第13步后的局面，调用 AI（内嵌实现）看它会选哪一步
// 编译：g++ test_ai.cpp -o test_ai.exe
#include <stdio.h>
#include <string.h>
#include <windows.h>

// ---- gomoku.h 的 stub ----
#define MAX_BOARD 15
int BOARD_SIZE = 15;
int board[MAX_BOARD][MAX_BOARD];

// ---- settings.h 的 stub ----
typedef struct {
    int ai_difficulty;
} GameSettings;
GameSettings game_settings;

// ---- 基础函数 ----
int check_win(int row, int col, int color);

// ============ 以下为 ai.cpp 核心逻辑复制 ============
#define SCORE_FIVE    100000
#define SCORE_LIVE4   10000
#define SCORE_RUSH4   5000
#define SCORE_LIVE3   3000
#define SCORE_SLEEP3  1000
#define SCORE_LIVE2   500
#define SCORE_SLEEP2  100
#define SCORE_LIVE1   50

static int eval_dir(int row, int col, int dr, int dc, int color)
{
    int count = 1;
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
        if (open_ends == 1) return 10;
        return 0;
    }
    return 0;
}

static int eval_position(int row, int col, int color)
{
    int dirs[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};
    int total = 0;
    for (int d = 0; d < 4; d++)
        total += eval_dir(row, col, dirs[d][0], dirs[d][1], color);
    return total;
}

void ai_decide(int* best_row, int* best_col)
{
    *best_row = -1;
    *best_col = -1;

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

            int attack = eval_position(r, c, 2);
            int defense = eval_position(r, c, 1);

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

#define AB_MAX_DEPTH    4
#define AB_TIME_BUDGET  250
#define AB_INF          100000000

static DWORD   ab_deadline;
static int     ab_abort;
static long    ab_nodes;

static int board_has_stone()
{
    for (int r = 0; r < BOARD_SIZE; r++)
        for (int c = 0; c < BOARD_SIZE; c++)
            if (board[r][c] != 0) return 1;
    return 0;
}

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

#define MAX_MOVES 225
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

static int evaluate_board(int color)
{
    int opp = (color == 1) ? 2 : 1;
    int my1 = 0, my2 = 0, op1 = 0, op2 = 0;

    for (int r = 0; r < BOARD_SIZE; r++)
    {
        for (int c = 0; c < BOARD_SIZE; c++)
        {
            if (board[r][c] != 0) continue;
            if (!is_near_stone(r, c)) continue;

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

static int negamax(int depth, int alpha, int beta, int color)
{
    if ((++ab_nodes & 1023) == 0 && GetTickCount() > ab_deadline)
    {
        ab_abort = 1;
        return 0;
    }

    if (depth == 0)
        return evaluate_board(color);

    int moves[MAX_MOVES][2];
    int n = gen_moves(moves);
    if (n == 0) return 0;

    order_moves(moves, n, color);

    int best = -AB_INF;
    for (int i = 0; i < n; i++)
    {
        int r = moves[i][0], c = moves[i][1];
        board[r][c] = color;

        int val;
        if (check_win(r, c, color))
            val = SCORE_FIVE + depth;
        else
            val = -negamax(depth - 1, -beta, -alpha, (color == 1) ? 2 : 1);

        board[r][c] = 0;

        if (val > best) best = val;
        if (best > alpha) alpha = best;
        if (alpha >= beta) break;

        if (ab_abort) break;
    }
    return best;
}

void ai_decide_alpha(int* best_row, int* best_col)
{
    *best_row = -1;
    *best_col = -1;

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

    int best_r = moves[0][0], best_c = moves[0][1];
    ab_deadline = GetTickCount() + AB_TIME_BUDGET;
    ab_abort = 0;
    ab_nodes = 0;

    for (int depth = 1; depth <= AB_MAX_DEPTH; depth++)
    {
        if (GetTickCount() > ab_deadline) break;

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
            if (ab_abort) break;
        }

        if (found)
        {
            *best_row = best_r;
            *best_col = best_c;
        }
        if (ab_abort) break;
    }
}

void ai_get_move(int* best_row, int* best_col)
{
    if (game_settings.ai_difficulty == 1)
        ai_decide_alpha(best_row, best_col);
    else
        ai_decide(best_row, best_col);
}

// ============ 辅助：胜负判断 ============
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

int check_win(int row, int col, int color)
{
    int dirs[4][2] = {{0,1},{1,0},{1,1},{1,-1}};
    for (int d = 0; d < 4; d++)
    {
        int dr = dirs[d][0], dc = dirs[d][1];
        int cnt = 1 + count_in_direction(row, col, dr, dc, color)
                    + count_in_direction(row, col, -dr, -dc, color);
        if (cnt >= 5) return 1;
    }
    return 0;
}

// ============ 测试入口 ============
// 逐手回放棋谱，每步黑棋落子后（轮到白棋前）问当前 AI 会选哪一手，
// 与棋谱中实际的白棋走法对比，判断棋谱是否为当前 AI 代码所下。
int main(int argc, char* argv[])
{
    const char* path = (argc > 1) ? argv[1] : "replays\\replay_test.gmk";
    FILE* fp = fopen(path, "r");
    if (!fp) { printf("无法打开棋谱\n"); return 1; }
    int bs, mc, res;
    char date[30];
    fscanf(fp, "%d", &bs);
    fscanf(fp, " %[^\n]", date);
    fscanf(fp, "%d", &mc);
    fscanf(fp, "%d", &res);
    printf("棋盘=%d 步数=%d 结果=%d 日期=%s\n", bs, mc, res, date);

    int rows[225], cols[225];
    for (int i = 0; i < mc; i++)
        fscanf(fp, "%d %d", &rows[i], &cols[i]);
    fclose(fp);

    BOARD_SIZE = bs;

    printf("步数 | 棋谱白棋走法 | 简单AI推荐    | 困难AI推荐\n");
    printf("-----+--------------+---------------+--------------\n");

    for (int i = 0; i < mc; i++)
    {
        int color = (i % 2 == 0) ? 1 : 2;

        if (color == 1)  // 黑棋：直接按棋谱落子
        {
            board[rows[i]][cols[i]] = 1;
            continue;
        }

        // 白棋：先让两个 AI 在"当前局面"下决策，再与实际走法对比
        int e1, c1, h1, d1;
        game_settings.ai_difficulty = 0;
        ai_get_move(&e1, &c1);
        game_settings.ai_difficulty = 1;
        ai_get_move(&h1, &d1);

        printf("%2d   | (%2d,%2d)       | (%2d,%2d)%s | (%2d,%2d)%s\n",
               i + 1, rows[i], cols[i],
               e1, c1, (e1 == rows[i] && c1 == cols[i]) ? " ✓" : "",
               h1, d1, (h1 == rows[i] && d1 == cols[i]) ? " ✓" : "");

        // 再落实际的白棋
        board[rows[i]][cols[i]] = 2;
    }

    printf("\n===== 最后局面 =====\n");
    for (int r = 0; r < BOARD_SIZE; r++)
    {
        for (int c = 0; c < BOARD_SIZE; c++)
        {
            char ch = '.';
            if (board[r][c] == 1) ch = 'X';
            else if (board[r][c] == 2) ch = 'O';
            printf("%c ", ch);
        }
        printf("\n");
    }

    // 验证最后局面是否有白棋五连
    printf("\n===== 最终局面是否白棋五连胜 =====\n");
    int five = 0;
    for (int r = 0; r < BOARD_SIZE && !five; r++)
        for (int c = 0; c < BOARD_SIZE && !five; c++)
            if (board[r][c] == 2 && check_win(r, c, 2))
                { printf("存在白棋五连: (%d,%d)\n", r, c); five = 1; }
    if (!five) printf("不存在白棋五连 —— result=2 只能来自超时判负路径\n");

    // 最后一手(10,2)评分
    printf("\n===== 最后一手(10,2)的评估 =====\n");
    printf("白落(10,2)进攻分: %d\n", eval_position(10, 2, 2));
    printf("黑落(10,2)防守分: %d\n", eval_position(10, 2, 1));
    printf("简单AI评分(10,2): %d\n", eval_position(10,2,1)*11/10 + eval_position(10,2,2));

    return 0;
}

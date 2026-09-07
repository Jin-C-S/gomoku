#!/usr/bin/env python3
"""
分析五子棋局面，评估红点位置是否为alpha-beta算法的最优解。
使用与ai.cpp相同的评估函数和搜索逻辑。
"""

import sys
import time

# 棋盘大小
BOARD_SIZE = 15

# 评分表
SCORE_FIVE = 100000
SCORE_LIVE4 = 10000
SCORE_RUSH4 = 5000
SCORE_LIVE3 = 3000
SCORE_SLEEP3 = 1000
SCORE_LIVE2 = 500
SCORE_SLEEP2 = 100
SCORE_LIVE1 = 50

# 搜索参数
AB_MAX_DEPTH = 4
AB_TIME_BUDGET = 0.25  # 250ms
AB_INF = 100000000

# 当前棋盘状态（从回放文件重建）
# 0=空, 1=黑, 2=白
board = [[0] * BOARD_SIZE for _ in range(BOARD_SIZE)]

# 落子历史（从回放文件）
moves = [
    (7, 7),   # 1. 黑
    (6, 6),   # 2. 白
    (7, 6),   # 3. 黑
    (7, 5),   # 4. 白
    (8, 6),   # 5. 黑
    (5, 7),   # 6. 白
    (8, 4),   # 7. 黑
    (4, 8),   # 8. 白
    (3, 9),   # 9. 黑
    (1, 8),   # 10. 白
    (3, 8),   # 11. 黑
    (7, 3),   # 12. 白
    (8, 5),   # 13. 黑
    (10, 2),  # 14. 白（红点位置）
]

# 重建棋盘
for i, (r, c) in enumerate(moves):
    color = 1 if i % 2 == 0 else 2  # 黑=1, 白=2
    board[r][c] = color

def print_board():
    """打印棋盘"""
    print("   ", end="")
    for c in range(BOARD_SIZE):
        print(f"{c:2d}", end=" ")
    print()
    for r in range(BOARD_SIZE):
        print(f"{r:2d} ", end="")
        for c in range(BOARD_SIZE):
            if board[r][c] == 0:
                print(" .", end=" ")
            elif board[r][c] == 1:
                print(" X", end=" ")  # 黑
            else:
                print(" O", end=" ")  # 白
        print()

def eval_dir(row, col, dr, dc, color):
    """评估某个方向上的棋型得分"""
    count = 1  # 当前假设的落子

    # 正方向统计
    r, c = row + dr, col + dc
    while 0 <= r < BOARD_SIZE and 0 <= c < BOARD_SIZE and board[r][c] == color:
        count += 1
        r += dr
        c += dc
    fwd_open = (0 <= r < BOARD_SIZE and 0 <= c < BOARD_SIZE and board[r][c] == 0)

    # 反方向统计
    r, c = row - dr, col - dc
    while 0 <= r < BOARD_SIZE and 0 <= c < BOARD_SIZE and board[r][c] == color:
        count += 1
        r -= dr
        c -= dc
    bwd_open = (0 <= r < BOARD_SIZE and 0 <= c < BOARD_SIZE and board[r][c] == 0)

    open_ends = (1 if fwd_open else 0) + (1 if bwd_open else 0)

    # 根据棋型评分
    if count >= 5:
        return SCORE_FIVE
    if count == 4:
        if open_ends == 2:
            return SCORE_LIVE4
        if open_ends == 1:
            return SCORE_RUSH4
        return 0
    if count == 3:
        if open_ends == 2:
            return SCORE_LIVE3
        if open_ends == 1:
            return SCORE_SLEEP3
        return 0
    if count == 2:
        if open_ends == 2:
            return SCORE_LIVE2
        if open_ends == 1:
            return SCORE_SLEEP2
        return 0
    if count == 1:
        if open_ends == 2:
            return SCORE_LIVE1
        if open_ends == 1:
            return 10
        return 0
    return 0

def eval_position(row, col, color):
    """评估在 (row,col) 落 color 棋的总得分（4个方向合计）"""
    dirs = [(0, 1), (1, 0), (1, 1), (1, -1)]
    total = 0
    for dr, dc in dirs:
        total += eval_dir(row, col, dr, dc, color)
    return total

def is_near_stone(r, c):
    """(r,c) 的 2 格范围内是否存在棋子"""
    for dr in range(-2, 3):
        for dc in range(-2, 3):
            nr, nc = r + dr, c + dc
            if 0 <= nr < BOARD_SIZE and 0 <= nc < BOARD_SIZE and board[nr][nc] != 0:
                return True
    return False

def gen_moves():
    """生成候选点：所有已有棋子 2 格范围内的空位"""
    moves = []
    for r in range(BOARD_SIZE):
        for c in range(BOARD_SIZE):
            if board[r][c] == 0 and is_near_stone(r, c):
                moves.append((r, c))
    return moves

def order_moves(moves, color):
    """把候选点按"在该处落子对当前方的价值"降序排序"""
    opp = 2 if color == 1 else 1
    move_vals = []
    for r, c in moves:
        av = eval_position(r, c, color)
        dv = eval_position(r, c, opp)
        val = max(av, dv)
        move_vals.append((val, r, c))
    # 降序排序
    move_vals.sort(reverse=True, key=lambda x: x[0])
    return [(r, c) for _, r, c in move_vals]

def check_win(row, col, color):
    """检查在(row,col)落color棋后是否获胜"""
    dirs = [(0, 1), (1, 0), (1, 1), (1, -1)]
    for dr, dc in dirs:
        count = 1
        # 正方向
        r, c = row + dr, col + dc
        while 0 <= r < BOARD_SIZE and 0 <= c < BOARD_SIZE and board[r][c] == color:
            count += 1
            r += dr
            c += dc
        # 反方向
        r, c = row - dr, col - dc
        while 0 <= r < BOARD_SIZE and 0 <= c < BOARD_SIZE and board[r][c] == color:
            count += 1
            r -= dr
            c -= dc
        if count >= 5:
            return True
    return False

def evaluate_board(color):
    """叶子局面打分：返回"当前行动方 color"视角的分值"""
    opp = 2 if color == 1 else 1
    my1, my2, op1, op2 = 0, 0, 0, 0

    for r in range(BOARD_SIZE):
        for c in range(BOARD_SIZE):
            if board[r][c] != 0:
                continue
            if not is_near_stone(r, c):
                continue

            mv = eval_position(r, c, color)
            ov = eval_position(r, c, opp)

            if mv > my1:
                my2 = my1
                my1 = mv
            elif mv > my2:
                my2 = mv

            if ov > op1:
                op2 = op1
                op1 = ov
            elif ov > op2:
                op2 = ov

    return (my1 + my2) - (op1 + op2)

def negamax(depth, alpha, beta, color, start_time):
    """negamax + alpha-beta 递归搜索"""
    if time.time() - start_time > AB_TIME_BUDGET:
        return 0  # 超时

    if depth == 0:
        return evaluate_board(color)

    moves = gen_moves()
    if not moves:
        return 0

    moves = order_moves(moves, color)

    best = -AB_INF
    for r, c in moves:
        board[r][c] = color

        if check_win(r, c, color):
            val = SCORE_FIVE + depth
        else:
            val = -negamax(depth - 1, -beta, -alpha, 2 if color == 1 else 1, start_time)

        board[r][c] = 0

        if val > best:
            best = val
        if best > alpha:
            alpha = best
        if alpha >= beta:
            break

        if time.time() - start_time > AB_TIME_BUDGET:
            break

    return best

def ai_decide_alpha(color):
    """AI决策：alpha-beta剪枝"""
    start_time = time.time()

    # 空棋盘：首步落天元
    if all(board[r][c] == 0 for r in range(BOARD_SIZE) for c in range(BOARD_SIZE)):
        return BOARD_SIZE // 2, BOARD_SIZE // 2

    moves = gen_moves()
    if not moves:
        return -1, -1
    if len(moves) == 1:
        return moves[0]

    # 快速路径：能立即五连
    for r, c in moves:
        board[r][c] = color
        win = check_win(r, c, color)
        board[r][c] = 0
        if win:
            return r, c

    moves = order_moves(moves, color)

    # 迭代加深
    best_r, best_c = moves[0]
    for depth in range(1, AB_MAX_DEPTH + 1):
        if time.time() - start_time > AB_TIME_BUDGET:
            break

        alpha = -AB_INF
        beta = AB_INF
        found = False

        for r, c in moves:
            board[r][c] = color

            if check_win(r, c, color):
                val = SCORE_FIVE + depth
            else:
                val = -negamax(depth - 1, -beta, -alpha, 2 if color == 1 else 1, start_time)

            board[r][c] = 0

            if val > alpha:
                alpha = val
                best_r, best_c = r, c
                found = True

            if time.time() - start_time > AB_TIME_BUDGET:
                break

        if found:
            pass  # 更新了best_r, best_c

        if time.time() - start_time > AB_TIME_BUDGET:
            break

    return best_r, best_c

def main():
    print("当前棋盘状态：")
    print_board()
    print()

    # 当前局面：白棋刚落在(10,2)
    print("当前局面：白棋刚落在 (10,2)")
    print()

    # 移除红点位置的棋子，重新分析
    board[10][2] = 0

    print("移除红点位置后，使用alpha-beta算法分析白棋最佳落子...")
    print()

    # 使用alpha-beta算法计算最佳落子
    best_r, best_c = ai_decide_alpha(2)  # 白棋=2

    print(f"Alpha-beta算法建议的最佳落子位置: ({best_r}, {best_c})")
    print(f"红点位置: (10, 2)")
    print()

    if (best_r, best_c) == (10, 2):
        print("✓ 红点位置 (10,2) 是alpha-beta算法的最优解")
    else:
        print("✗ 红点位置 (10,2) 不是alpha-beta算法的最优解")
        print(f"  最优解应该是 ({best_r}, {best_c})")

    print()
    print("评估红点位置 (10,2) 的得分:")
    score_10_2 = eval_position(10, 2, 2)
    print(f"  白棋在(10,2)的得分: {score_10_2}")

    print()
    print("评估建议位置 ({}, {}) 的得分:".format(best_r, best_c))
    score_best = eval_position(best_r, best_c, 2)
    print(f"  白棋在({best_r},{best_c})的得分: {score_best}")

    print()
    print("分析黑棋在第8行的活三威胁:")
    print("  黑棋在(8,4), (8,5), (8,6)形成活三")
    print("  白棋应该阻止这个活三")
    print()

    # 评估阻止活三的位置
    print("评估阻止活三的位置:")
    for pos in [(8, 3), (8, 7)]:
        r, c = pos
        score = eval_position(r, c, 2)
        print(f"  ({r},{c}): 得分 {score}")

    print()
    print("结论:")
    if (best_r, best_c) == (10, 2):
        print("红点位置 (10,2) 是最优解，但可能不是最直接的防守位置")
    else:
        print(f"红点位置 (10,2) 不是最优解，最优解是 ({best_r},{best_c})")
        print("这可能是因为alpha-beta算法考虑了更深远的搜索和整体局面")

if __name__ == "__main__":
    main()
#ifndef NETWORK_H
#define NETWORK_H

#include <winsock2.h>

#define NET_PORT 9527

// 网络信号：row 和 col 取相同负值作为命令标识
#define NET_SIGNAL_RESTART  -1   // 重新开始
#define NET_SIGNAL_UNDO_REQ -2   // 悔棋请求
#define NET_SIGNAL_UNDO_ACK -3   // 悔棋同意
#define NET_SIGNAL_UNDO_NACK -4  // 悔棋拒绝
#define NET_SIGNAL_DRAW_REQ -5   // 和棋请求
#define NET_SIGNAL_DRAW_ACK -6   // 和棋同意
#define NET_SIGNAL_DRAW_NACK -7  // 和棋拒绝

// 超时（秒）
#define NET_TIMEOUT_SEC 30

// 网络消息（8字节）
#pragma pack(1)
typedef struct {
    int row;
    int col;
} NetMove;
#pragma pack()

// 网络状态
extern int network_role;        // 0=未连接, 1=主机(Host), 2=加入(Client)
extern int network_connected;   // 0=未连, 1=已连
extern SOCKET net_sock;         // 通信 socket
extern int net_waiting_response; // 0=空闲, 1=等待悔棋回应, 2=等待和棋回应

// 初始化 Winsock
int  net_init();

// 清理 Winsock
void net_cleanup();

// 创建服务端（阻塞等待客户端连接）
// port: 监听端口
// 返回 1=成功, 0=失败
int  net_host_start(int port);

// 连接服务端
// ip: 服务端 IP 地址, port: 端口
// 返回 1=成功, 0=失败
int  net_client_connect(const char* ip, int port);

// 发送落子
// 返回 1=成功, 0=失败
int  net_send_move(int row, int col);

// 非阻塞接收落子
// move: 输出参数
// 返回: 1=收到数据, 0=无数据, -1=连接断开/错误
int  net_recv_move(NetMove* move);

// 断开连接
void net_disconnect();

#endif // NETWORK_H

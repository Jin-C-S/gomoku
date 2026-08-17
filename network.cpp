#include <stdio.h>
#include <string.h>
#include "network.h"

// 全局变量定义
int network_role = 0;           // 0=未连接, 1=主机(Host), 2=加入(Client)
int network_connected = 0;      // 0=未连, 1=已连
SOCKET net_sock = INVALID_SOCKET;
int net_waiting_response = 0;   // 0=空闲, 1=等待悔棋回应, 2=等待和棋回应

// 初始化 Winsock
int net_init()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        printf("WSAStartup 失败\n");
        return 0;
    }
    return 1;
}

// 清理 Winsock
void net_cleanup()
{
    WSACleanup();
}

// 创建 TCP 服务端，阻塞等待客户端连接
int net_host_start(int port)
{
    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == INVALID_SOCKET)
    {
        printf("创建 socket 失败\n");
        return 0;
    }

    // 允许端口重用
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR,
               (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        printf("bind 失败: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        return 0;
    }

    if (listen(listen_sock, 1) == SOCKET_ERROR)
    {
        printf("listen 失败\n");
        closesocket(listen_sock);
        return 0;
    }

    printf("等待对手连接，端口 %d ...\n", port);

    // 阻塞等待客户端连接
    net_sock = accept(listen_sock, NULL, NULL);
    closesocket(listen_sock);  // 不再需要监听 socket

    if (net_sock == INVALID_SOCKET)
    {
        printf("accept 失败\n");
        return 0;
    }

    // 设置非阻塞模式
    u_long mode = 1;
    ioctlsocket(net_sock, FIONBIO, &mode);

    printf("对手已连接！\n");
    return 1;
}

// 连接服务端
int net_client_connect(const char* ip, int port)
{
    net_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (net_sock == INVALID_SOCKET)
    {
        printf("创建 socket 失败\n");
        return 0;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port = htons(port);

    printf("正在连接 %s:%d ...\n", ip, port);

    if (connect(net_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        printf("连接失败: %d\n", WSAGetLastError());
        closesocket(net_sock);
        net_sock = INVALID_SOCKET;
        return 0;
    }

    // 设置非阻塞模式
    u_long mode = 1;
    ioctlsocket(net_sock, FIONBIO, &mode);

    printf("已连接到主机！\n");
    return 1;
}

// 发送落子
int net_send_move(int row, int col)
{
    if (net_sock == INVALID_SOCKET) return 0;

    NetMove move;
    move.row = htonl(row);
    move.col = htonl(col);

    int ret = send(net_sock, (const char*)&move, sizeof(move), 0);
    return (ret == sizeof(move)) ? 1 : 0;
}

// 非阻塞接收落子
// 返回: 1=收到数据, 0=无数据, -1=连接断开/错误
int net_recv_move(NetMove* move)
{
    if (net_sock == INVALID_SOCKET) return -1;

    // 用 select 检测是否有数据可读
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(net_sock, &fds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    int ret = select(0, &fds, NULL, NULL, &tv);
    if (ret == 0) return 0;  // 无数据
    if (ret == SOCKET_ERROR) return -1;

    // 有数据可读
    char buf[sizeof(NetMove)];
    int n = recv(net_sock, buf, sizeof(buf), 0);

    if (n == 0) return -1;  // 连接关闭
    if (n != sizeof(NetMove)) return -1;  // 数据不完整或错误

    NetMove* p = (NetMove*)buf;
    move->row = ntohl(p->row);
    move->col = ntohl(p->col);
    return 1;
}

// 断开连接
void net_disconnect()
{
    if (net_sock != INVALID_SOCKET)
    {
        closesocket(net_sock);
        net_sock = INVALID_SOCKET;
    }
    network_connected = 0;
    network_role = 0;
    net_waiting_response = 0;
}

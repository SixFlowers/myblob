#pragma once
#include <chrono>

namespace myblob::network {

struct TCPSettings {
    int nonBlocking = 1;//非阻塞模式。1 = socket 设为非阻塞，读写不等待立即返回（配合事件循环使用）。
    int noDelay = 0;//TCP_NODELAY。1 = 禁用 Nagle 算法，小包立即发送，降低延迟；0 = 默认启用 Nagle，攒够数据再发。
    int keepAlive = 1;//TCP_KEEPALIVE。1 = 启用 keep-alive 机制，检测连接是否存活；0 = 关闭。
    int keepIdle = 1;//TCP_KEEPIDLE。空闲多久后开始发送 keep-alive 探测包（秒）。
    int keepIntvl = 1;//TCP_KEEPINTVL。两次 keep-alive 探测包之间的间隔（秒）。
    int keepCnt = 1;//TCP_KEEPCNT。连续发送多少个 keep-alive 探测包后仍未响应则断开连接。
    int recvBuffer = 0;//接收缓冲区大小（字节）。0 = 使用系统默认值。
    int recvNoWait = 0;//recv 时设 MSG_DONTWAIT，减少一次系统调用（数据立即可用时直接返回）
    int mss = 0;//最大段大小（字节）。0 = 使用系统默认值。
    int reusePorts = 0;//SO_REUSEPORT。1 = 允许多个套接字绑定到同一端口（负载均衡）；0 = 关闭。
    int linger = 1;//SO_LINGER。1 = 关闭时等待数据发送完成；0 = 立即关闭。
    std::chrono::milliseconds timeout = std::chrono::milliseconds(500);//连接超时时间（毫秒）。
    int reuse = 1;//SO_REUSEADDR。1 = 允许地址重用；0 = 关闭。
};

}

#pragma once
#include "network/socket.hpp"
#include "network/tcp_settings.hpp"
#include "network/original_message.hpp"
#include <cstdint>

namespace myblob::network {

struct HTTPSMessage {
    int fd;                          // Socket 文件描述符
    uint64_t chunkSize;              // BIO 缓冲区大小
    OriginalMessage* originalMessage; // 原始消息
    TCPSettings tcpSettings;         // TCP 配置
    Socket::Request* request;        // Socket 请求（指向 Request 对象的指针）
};

}

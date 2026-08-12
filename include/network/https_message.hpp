#pragma once
#include "network/http_message.hpp"
#include <cstdint>

namespace myblob::network {

class TLSConnection;
class ConnectionManager;

/// HTTPS 消息任务：在 HTTP 基础上增加 TLS 加密层
/// 状态机流程：Init → TLSHandshake → InitSending → Sending → InitReceiving → Receiving → TLSShutdown → Finished/Aborted
///
/// 与 HTTPMessage 的区别：
/// 1. 多了 TLSHandshake 状态（SSL 握手）
/// 2. 多了 TLSShutdown 状态（SSL 关闭）
/// 3. 发送/接收不直接用 socket，而是通过 TLSConnection 加密/解密
struct HTTPSMessage : public HTTPMessage {
    TLSConnection* tlsLayer;  //TLS 加密层（封装 OpenSSL SSL 对象）
    int32_t fd;               //socket 文件描述符

    HTTPSMessage(OriginalMessage* sendingMessage, 
                 TCPSettings& tcpSettings, 
                 uint32_t chunksize);
    ~HTTPSMessage() override;
    MessageState execute(ConnectionManager& connectionManager) override;
    void reset(ConnectionManager& socket, bool aborted);
};

} // namespace myblob::network

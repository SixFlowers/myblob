#pragma once
#include "network/http_message.hpp"

namespace myblob::network {

class TLSConnection;
class ConnectionManager;

/// 实现HTTPS消息往返
struct HTTPSMessage : public HTTPMessage {
    /// TLS层
    TLSConnection* tlsLayer;
    /// 文件描述符
    int32_t fd;

    /// 构造函数
    HTTPSMessage(OriginalMessage* sendingMessage, 
                 TCPSettings& tcpSettings, 
                 uint32_t chunksize);
    /// 析构函数
    ~HTTPSMessage() override = default;
    /// 消息执行回调
    MessageState execute(ConnectionManager& connectionManager) override;
    /// 重置以重新开始
    void reset(ConnectionManager& socket, bool aborted);
};

} // namespace myblob::network

#pragma once
#include "network/message_task.hpp"
#include "network/http_helper.hpp"
#include <memory>

namespace myblob::network {

class ConnectionManager;

/// 实现HTTP消息往返
struct HTTPMessage : public MessageTask {
    /// HTTP信息头
    std::unique_ptr<HttpHelper::Info> info;

    /// 构造函数
    HTTPMessage(OriginalMessage* sendingMessage, 
                TCPSettings& tcpSettings, 
                uint32_t chunkSize);
    /// 析构函数
    ~HTTPMessage() override = default;
    /// 消息执行回调
    MessageState execute(ConnectionManager& connectionManager) override;
    /// 重置以重新开始
    void reset(ConnectionManager& connectionManager, bool aborted);
};

} // namespace myblob::network

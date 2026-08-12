#pragma once
#include "network/message_task.hpp"
#include "network/connection.hpp"
#include "network/http_helper.hpp"
#include <memory>

namespace myblob::network {

class ConnectionManager;

/// HTTP 消息任务：实现一次明文 HTTP 请求-响应往返
/// 状态机流程：Init → InitSending → Sending → InitReceiving → Receiving → Finished/Aborted
///
/// execute() 的调用模式（被 TaskedSendReceiver 事件循环驱动）：
///   第1次 execute(): Init → 获取连接 → InitSending → 构造 Request → socket.send() → 返回 Sending
///   [等待 complete() 返回]
///   第2次 execute(): Sending → 检查上次发送结果 → 构造下一个 Request → socket.send() → 返回 Sending
///   [等待 complete() 返回]
///   ...重复直到发送完毕...
///   第N次 execute(): Sending → 发送完毕 → InitReceiving → 构造 Request → socket.recv() → 返回 Receiving
///   [等待 complete() 返回]
///   第N+1次 execute(): Receiving → 检查上次接收结果 → HttpHelper::finished() → Finished/Aborted
struct HTTPMessage : public MessageTask {
    std::unique_ptr<HttpHelper::Info> info;    //HTTP 响应解析结果（状态码、Content-Length 等）
    std::unique_ptr<Connection> connection_;    //从 ConnectionManager 获取的连接（单线程，独占所有权）

    HTTPMessage(OriginalMessage* sendingMessage, 
                TCPSettings& tcpSettings, 
                uint32_t chunkSize);
    ~HTTPMessage() override = default;
    MessageState execute(ConnectionManager& connectionManager) override;
    //重置状态机，准备重试（aborted=true 则直接放弃）
    void reset(ConnectionManager& connectionManager, bool aborted);
};

} // namespace myblob::network

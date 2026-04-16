#pragma once
#include "network/tcp_settings.hpp"
#include "network/original_message.hpp"
#include "network/socket.hpp"
#include "network/message_state.hpp"
#include "network/message_failure_code.hpp"
#include "utils/data_vector.hpp"
#include <memory>
#include <string_view>

namespace myblob::network {

class ConnectionManager;
class TLSConnection;
class HTTPMessage;
class HTTPSMessage;

/// 消息任务基类
/// 每次执行execute后都会向io_uring队列添加新请求，需要提交
struct MessageTask {
    /// 消息任务类型
    enum class Type : uint8_t {
        HTTP,
        HTTPS
    };

    /// 原始发送消息
    OriginalMessage* originalMessage;
    /// TCP设置
    TCPSettings& tcpSettings;
    /// 发送请求
    std::unique_ptr<Socket::Request> request;
    /// 发送缓冲区偏移
    int64_t sendBufferOffset;
    /// 接收缓冲区偏移
    int64_t receiveBufferOffset;
    /// 块大小
    uint32_t chunkSize;
    /// 失败次数
    uint16_t failures;
    /// 消息任务类型
    Type type;

    /// 接收和发送期间的失败限制
    static constexpr uint16_t failuresMax = 32;
    /// 连接失败限制
    static constexpr uint16_t connectionFailuresMax = 4;

    /// 纯虚回调函数
    virtual MessageState execute(ConnectionManager& connectionManager) = 0;
    /// 纯虚析构函数
    virtual ~MessageTask() {}

    /// 根据发送消息构建消息任务
    template <typename... Args>
    static std::unique_ptr<MessageTask> buildMessageTask(
        OriginalMessage* sendingMessage, 
        Args&&... args) {
        // 前向声明在cpp文件中实现
        extern std::unique_ptr<MessageTask> buildMessageTaskImpl(
            OriginalMessage* sendingMessage, 
            TCPSettings& tcpSettings, 
            uint32_t chunkSize);
        return buildMessageTaskImpl(sendingMessage, 
            std::forward<Args>(args)...);
    }

protected:
    /// 构造函数
    MessageTask(OriginalMessage* sendingMessage, 
                TCPSettings& tcpSettings, 
                uint32_t chunkSize);
};

} // namespace myblob::network

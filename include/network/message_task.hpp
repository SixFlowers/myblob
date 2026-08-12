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

/// 消息任务基类：代表一次 HTTP/HTTPS 请求-响应往返
/// 核心设计：每次 execute() 只推进一步状态机，挂一个 I/O 到内核后返回
/// 等待 TaskedSendReceiver 的事件循环中 complete() 返回结果后，再调一次 execute()
///
/// 生命周期：buildMessageTask() 创建 → execute() 被反复调用 → Finished/Aborted 结束
struct MessageTask {
    /// 消息任务类型（决定用 HTTP 还是 HTTPS）
    enum class Type : uint8_t {
        HTTP,     //明文 HTTP，直接用 socket send/recv
        HTTPS     //HTTPS，数据经过 TLS 加密/解密
    };

    OriginalMessage* originalMessage;     //原始消息：包含 HTTP 请求头、PUT 数据、Provider 信息、结果
    TCPSettings& tcpSettings;              //TCP 配置：超时、keepalive 等
    std::unique_ptr<Socket::Request> request;  //当前挂到内核的 I/O 请求（send 或 recv）
    int64_t sendBufferOffset;               //发送偏移：已发送多少字节（用于大文件分块发送）
    int64_t receiveBufferOffset;            //接收偏移：已接收多少字节（用于判断是否收完）
    uint32_t chunkSize;                     //分块大小：每次 send/recv 的最大字节数
    uint16_t failures;                      //累计失败次数（超过上限则 Aborted）
    Type type;                               //HTTP 还是 HTTPS

    static constexpr uint16_t failuresMax = 32;           //I/O 失败上限（超时、发送/接收错误）
    static constexpr uint16_t connectionFailuresMax = 4;  //连接失败上限（socket 创建、DNS、握手）

    //纯虚函数：推进状态机一步
    //每次调用做一件事（获取连接/发送一块/接收一块），然后返回当前状态
    //由 TaskedSendReceiver::sendReceive() 的事件循环驱动
    virtual MessageState execute(ConnectionManager& connectionManager) = 0;
    virtual ~MessageTask() {}

    //工厂方法：根据 Provider 是否 HTTPS 创建 HTTPMessage 或 HTTPSMessage
    //模板 + extern 实现的模式是为了避免头文件循环依赖
    template <typename... Args>
    static std::unique_ptr<MessageTask> buildMessageTask(
        OriginalMessage* sendingMessage, 
        Args&&... args) {
        extern std::unique_ptr<MessageTask> buildMessageTaskImpl(
            OriginalMessage* sendingMessage, 
            TCPSettings& tcpSettings, 
            uint32_t chunkSize);
        return buildMessageTaskImpl(sendingMessage, 
            std::forward<Args>(args)...);
    }

protected:
    //构造函数（由 HTTPMessage/HTTPSMessage 调用）
    MessageTask(OriginalMessage* sendingMessage, 
                TCPSettings& tcpSettings, 
                uint32_t chunkSize);
};

} // namespace myblob::network

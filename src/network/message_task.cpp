#include "network/message_task.hpp"
#include "network/http_message.hpp"
#include "network/https_message.hpp"
#include "cloud/provider.hpp"

namespace myblob::network {

/// 构造函数
MessageTask::MessageTask(OriginalMessage* sendingMessage, 
                         TCPSettings& tcpSettings, 
                         uint32_t chunkSize)
    : originalMessage(sendingMessage)
    , tcpSettings(tcpSettings)
    , sendBufferOffset(0)
    , receiveBufferOffset(0)
    , chunkSize(chunkSize)
    , failures(0) {
}

/// 工厂方法实现
std::unique_ptr<MessageTask> buildMessageTaskImpl(
    OriginalMessage* sendingMessage, 
    TCPSettings& tcpSettings, 
    uint32_t chunkSize) {
    
    if (sendingMessage->provider.isHTTPS()) {
        return std::make_unique<HTTPSMessage>(
            sendingMessage, tcpSettings, chunkSize);
    } else {
        return std::make_unique<HTTPMessage>(
            sendingMessage, tcpSettings, chunkSize);
    }
}

} // namespace myblob::network

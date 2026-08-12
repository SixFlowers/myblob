#include "network/message_task.hpp"
#include "network/http_message.hpp"
#include "network/https_message.hpp"
#include "cloud/provider.hpp"

namespace myblob::network {

//构造函数：初始化消息任务的公共字段
MessageTask::MessageTask(OriginalMessage* sendingMessage, 
                         TCPSettings& tcpSettings, 
                         uint32_t chunkSize)
    : originalMessage(sendingMessage)
    , tcpSettings(tcpSettings)
    , sendBufferOffset(0)          //还没发送任何数据
    , receiveBufferOffset(0)       //还没接收任何数据
    , chunkSize(chunkSize)         //每次 I/O 的最大字节数
    , failures(0) {                //还没失败过
}

//工厂方法实现：根据 Provider 是否 HTTPS 创建对应的消息任务
//HTTPS → HTTPSMessage（多了 TLS 握手/关闭状态）
//HTTP  → HTTPMessage（直接 socket 收发）
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

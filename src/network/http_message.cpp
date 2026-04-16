#include "network/http_message.hpp"
#include "network/connection_manager.hpp"
#include "cloud/provider.hpp"
#include <memory>

namespace myblob::network {

HTTPMessage::HTTPMessage(OriginalMessage* message, 
                         TCPSettings& tcpSettings, 
                         uint32_t chunkSize) 
    : MessageTask(message, tcpSettings, chunkSize)
    , info() {
    type = Type::HTTP;
}

MessageState HTTPMessage::execute(ConnectionManager& connectionManager) {
    // 简化版实现 - 使用现有的连接管理
    auto& state = originalMessage->result.state_;
    
    // 这里应该实现完整的状态机
    // 但为了简化，我们直接返回当前状态
    // 实际项目中需要实现连接、发送、接收的完整流程
    
    return state.load();
}

void HTTPMessage::reset(ConnectionManager& connectionManager, bool aborted) {
    if (!aborted) {
        originalMessage->result.getDataVector().clear();
        receiveBufferOffset = 0;
        sendBufferOffset = 0;
        info.reset();
        originalMessage->result.state_ = MessageState::Init;
    } else {
        originalMessage->result.state_ = MessageState::Aborted;
    }
    
    if (request && request->fd >= 0) {
        request->fd = -1;
    }
}

} // namespace myblob::network

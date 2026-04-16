#include "network/https_message.hpp"
#include "network/connection_manager.hpp"
#include "network/message_result.hpp"
#include <cassert>
#include <memory>
#include <utility>

namespace myblob::network {

using namespace std;

HTTPSMessage::HTTPSMessage(OriginalMessage* message, 
                           TCPSettings& tcpSettings, 
                           uint32_t chunkSize) 
    : HTTPMessage(message, tcpSettings, chunkSize)
    , tlsLayer(nullptr)
    , fd(-1) {
    type = Type::HTTPS;
}

MessageState HTTPSMessage::execute(ConnectionManager& connectionManager) {
    // 简化版：直接调用HTTPMessage的实现
    // 实际项目中需要添加TLS握手和加密传输
    return HTTPMessage::execute(connectionManager);
}

void HTTPSMessage::reset(ConnectionManager& connectionManager, bool aborted) {
    HTTPMessage::reset(connectionManager, aborted);
}

} // namespace myblob::network

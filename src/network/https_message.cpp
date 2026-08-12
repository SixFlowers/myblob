#include "network/https_message.hpp"
#include "cloud/provider.hpp"
#include "network/connection_manager.hpp"
#include "network/message_result.hpp"
#include "network/tls_connection.hpp"
#include "network/http_response.hpp"
#include <algorithm>
#include <cassert>
#include <memory>
#include <utility>

namespace myblob::network {

using namespace std;

HTTPSMessage::HTTPSMessage(OriginalMessage* message, 
                           TCPSettings& tcpSettings, 
                           uint32_t chunkSize) 
    : HTTPMessage(message, tcpSettings, chunkSize)
    , tlsLayer(nullptr)  //TLS 层延迟创建（Init 状态时创建）
    , fd(-1) {           //尚未获取 socket fd
    type = Type::HTTPS;
}

HTTPSMessage::~HTTPSMessage() {
    delete tlsLayer;
    tlsLayer = nullptr;
}

//HTTPS 状态机：在 HTTP 基础上增加 TLS 握手和关闭
//状态流转e
//  TLSHandshake → SSL 握手 → InitSending：
//  Init → 获取连接 + 创建 TLS 层 → TLSHandshak
//  InitSending / Sending → TLS 加密发送（不直接用 socket）
//  InitReceiving / Receiving → TLS 解密接收（不直接用 socket）
//  TLSShutdown → SSL 关闭 → Finished
MessageState HTTPSMessage::execute(ConnectionManager& connectionManager) {
    auto& state = originalMessage->result.state_;
    auto& failureCode = originalMessage->result.failureCode_;
    switch (state.load()) {
        //==================== Init：获取连接 + 创建 TLS 层 ====================
        case MessageState::Init: {
            try {
                //获取 HTTPS 连接（use_tls=true）
                connection_ = connectionManager.getConnection(
                    originalMessage->provider.getAddress(),
                    originalMessage->provider.getPort(),
                    true
                );
                if (!connection_ || !connection_->isConnected()) {
                    throw std::runtime_error("TLS connection failed");
                }
                fd = connection_->getSocket();
            } catch (std::exception&) {
                if (request) {
                    request->fd = -1;
                }
                failureCode |= static_cast<uint16_t>(MessageFailureCode::Socket);
                reset(connectionManager, failures++ > connectionFailuresMax);
                return execute(connectionManager);
            }
            //创建 TLS 层（如果还没创建）
            if (!tlsLayer) {
                tlsLayer = new TLSConnection(connectionManager.getTLSContext());
            }
            //初始化 TLS 层（绑定 socket fd 和 SSL 对象）
            if (!tlsLayer->init(this)) {
                failureCode |= static_cast<uint16_t>(MessageFailureCode::TLS);
                reset(connectionManager, failures++ > connectionFailuresMax);
                return execute(connectionManager);
            }
            state.store(MessageState::TLSHandshake);
            sendBufferOffset = 0;
        } // fallthrough

        //==================== TLSHandshake：SSL 握手 ====================
        case MessageState::TLSHandshake: {
            auto status = tlsLayer->connect(connectionManager);
            if (status == TLSConnection::Progress::Finished) {
                //握手成功：进入发送阶段
                state.store(MessageState::InitSending);
            } else if (status == TLSConnection::Progress::Aborted) {
                //握手失败
                failureCode |= static_cast<uint16_t>(MessageFailureCode::TLS);
                reset(connectionManager, failures++ > failuresMax);
                return execute(connectionManager);
            } else {
                //握手仍在进行（NeedRead/NeedWrite），等待下次 execute()
                return state.load();
            }
        } // fallthrough

        //==================== InitSending / Sending：TLS 加密发送 ====================
        case MessageState::InitSending:
        case MessageState::Sending: {
            //计算要发送的数据（与 HTTPMessage 相同的分块逻辑）
            const uint8_t* ptr = originalMessage->message->data() + sendBufferOffset;
            auto length = static_cast<int64_t>(originalMessage->message->size()) - sendBufferOffset;
            if (originalMessage->putLength > 0 && sendBufferOffset >= static_cast<int64_t>(originalMessage->message->size())) {
                ptr = originalMessage->putData + sendBufferOffset - static_cast<int64_t>(originalMessage->message->size());
                length = static_cast<int64_t>(originalMessage->putLength + originalMessage->message->size()) - sendBufferOffset;
            }

            //通过 TLS 层加密发送（不是直接 socket send）
            int64_t result = 0;
            auto status = tlsLayer->send(connectionManager, reinterpret_cast<const char*>(ptr), length, result);
            if (status == TLSConnection::Progress::Finished) {
                //发送成功：推进偏移
                sendBufferOffset += result;
                if (sendBufferOffset >= static_cast<int64_t>(originalMessage->message->size() + originalMessage->putLength)) {
                    //发送完毕：转入接收阶段
                    state.store(MessageState::InitReceiving);
                    receiveBufferOffset = 0;
                    auto& receive = originalMessage->result.getDataVector();
                    receive.clear();
                }
                return execute(connectionManager);
            }
            if (status == TLSConnection::Progress::Aborted) {
                //发送失败
                reset(connectionManager, failures++ > failuresMax);
                return execute(connectionManager);
            }
            //NeedRead/NeedWrite：等待下次 execute()
            state.store(MessageState::Sending);
            return state.load();
        }

        //==================== InitReceiving / Receiving：TLS 解密接收 ====================
        case MessageState::InitReceiving:
        case MessageState::Receiving: {
            auto& receive = originalMessage->result.getDataVector();
            auto reservedLength = receive.size() >= static_cast<uint64_t>(receiveBufferOffset)
                ? receive.size() - static_cast<uint64_t>(receiveBufferOffset)
                : 0;
            if (state.load() == MessageState::InitReceiving) {
                reservedLength = 0;  //首次接收没有预留空间
            }
            if (reservedLength == 0) {
                //需要更多缓冲区空间
                auto readSize = static_cast<uint64_t>(chunkSize);
                if (!receive.owned()) {
                    //外部缓冲区：不能超过容量
                    if (receive.capacity() <= receiveBufferOffset) {
                        failureCode |= static_cast<uint16_t>(MessageFailureCode::Recv);
                        state.store(MessageState::Aborted);
                        return state.load();
                    }
                    readSize = std::min<uint64_t>(chunkSize, receive.capacity() - receiveBufferOffset);
                } else if (receive.capacity() < receive.size() + chunkSize && info) {
                    //预分配空间（如果已知 Content-Length）
                    receive.reserve(std::max(info->length + info->headerLength + chunkSize, receive.capacity() + receive.capacity() / 2));
                }
                receive.resize(receive.size() + readSize);
                reservedLength = readSize;
            }
            //通过 TLS 层解密接收（不是直接 socket recv）
            int64_t result = 0;
            assert(reservedLength <= static_cast<uint64_t>(INT64_MAX));
            auto status = tlsLayer->recv(
                connectionManager,
                reinterpret_cast<char*>(receive.data() + receiveBufferOffset),
                static_cast<int64_t>(reservedLength),
                result
            );
            state.store(MessageState::Receiving);
            if (status == TLSConnection::Progress::Finished) {
                //接收成功：调整缓冲区，推进偏移
                receive.resize(static_cast<uint64_t>(receiveBufferOffset) + static_cast<uint64_t>(result));
                receiveBufferOffset += result;
                try {
                    //判断 HTTP 响应是否收完
                    if (HttpHelper::finished(receive.data(), static_cast<uint64_t>(receiveBufferOffset), info)) {
                        originalMessage->result.response_ = std::move(info);
                        if (HttpResponse::checkSuccess(originalMessage->result.response_->response.code)) {
                            //成功：进入 TLS 关闭阶段
                            state.store(MessageState::TLSShutdown);
                        } else {
                            //HTTP 错误：直接放弃（不关闭 TLS）
                            failureCode |= static_cast<uint16_t>(MessageFailureCode::HTTP);
                            reset(connectionManager, true);
                            return state.load();
                        }
                        return execute(connectionManager);
                    }
                    //还没收完：继续接收
                    return execute(connectionManager);
                } catch (std::exception&) {
                    failureCode |= static_cast<uint16_t>(MessageFailureCode::HTTP);
                    reset(connectionManager, failures++ > failuresMax);
                    return execute(connectionManager);
                }
            }
            if (status == TLSConnection::Progress::Aborted) {
                reset(connectionManager, failures++ > failuresMax);
                return execute(connectionManager);
            }
            //NeedRead/NeedWrite：等待下次 execute()
            return state.load();
        }

        //==================== TLSShutdown：SSL 关闭 ====================
        case MessageState::TLSShutdown: {
            auto status = tlsLayer->shutdown(connectionManager);
            if (status == TLSConnection::Progress::Finished || status == TLSConnection::Progress::Aborted) {
                //关闭完成（或强制关闭）：断开连接，标记完成
                if (connection_) {
                    connection_->disconnect();
                    connectionManager.returnConnection(std::move(connection_));
                }
                state.store(MessageState::Finished);
                return MessageState::Finished;
            }
            //关闭仍在进行：等待下次 execute()
            return state.load();
        }
        default:
            break;
    }
    return state.load();
}

//重置 HTTPS 状态机：先清理 TLS 层，再调用 HTTPMessage::reset()
void HTTPSMessage::reset(ConnectionManager& connectionManager, bool aborted) {
    if (tlsLayer) {
        tlsLayer->destroy();
        delete tlsLayer;
        tlsLayer = nullptr;
    }
    fd = -1;
    HTTPMessage::reset(connectionManager, aborted);
}

} // namespace myblob::network

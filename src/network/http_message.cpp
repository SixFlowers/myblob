#include "network/http_message.hpp"
#include "network/connection_manager.hpp"
#include "network/http_helper.hpp"
#include "network/http_response.hpp"
#include "cloud/provider.hpp"
#include <memory>
#include <algorithm>
#include <stdexcept>
#include <exception>
#include <sys/socket.h>

namespace myblob::network {

HTTPMessage::HTTPMessage(OriginalMessage* message, 
                         TCPSettings& tcpSettings, 
                         uint32_t chunkSize) 
    : MessageTask(message, tcpSettings, chunkSize)
    , info()           //尚未解析 HTTP 响应
    , connection_() {  //尚未获取连接
    type = Type::HTTP;
}

//HTTP 状态机：每次调用推进一步，挂一个 I/O 后返回
//被 TaskedSendReceiver::sendReceive() 的事件循环反复调用
//
//状态流转：
//  Init → 获取连接 → InitSending ──fallthrough──→ 构造 send Request
//  Sending → 检查上次结果 → 构造下一个 send Request / 转入 InitReceiving
//  InitReceiving ──fallthrough──→ 构造 recv Request
//  Receiving → 检查上次结果 → finished()? → Finished/Aborted / 继续接收
MessageState HTTPMessage::execute(ConnectionManager& connectionManager) {
    int32_t fd = -1;//fd 是局部变量，每次 execute() 调用都会重新赋值
    auto& state = originalMessage->result.state_;//state 和 failureCode 是引用，修改的就是 MessageResult 里的原值
    auto& failureCode = originalMessage->result.failureCode_;

    for (;;) {  // loop replaces recursive error-retry (bounded by failuresMax)
        switch (state.load()) {
        //==================== Init：获取连接 ====================
        case MessageState::Init: {
            try {
                //从连接池获取一个到目标主机的连接（可能复用已有连接）
                connection_ = connectionManager.getConnection(
                    originalMessage->provider.getAddress(),
                    originalMessage->provider.getPort(),
                    originalMessage->provider.isHTTPS()
                );
                if (!connection_ || !connection_->isConnected()) {
                    throw std::runtime_error("Connection failed");
                }
                fd = connection_->getSocket();// L48: 从 Connection 取出 sockfd，后面 send/recv 要用
            } catch (std::exception&) {// L49: 捕获 getConnection() 或上面的 throw
                //连接失败：记录错误，重试或放弃
                if (request)// L51: 如果之前有残留的 Request（重试场景）
                    request->fd = -1;// L52: 标记 fd 无效，防止 complete() 误处理
                failureCode |= static_cast<uint16_t>(MessageFailureCode::Socket);// L53: 记录 Socket 失败位
                reset(connectionManager, failures++ > connectionFailuresMax);
                continue;// L55: 递归调用！reset 后从新状态重新执行
            }
            state.store(MessageState::InitSending);
            sendBufferOffset = 0;// L58: 发送偏移归零，从头开始发
        } // fallthrough 到 InitSending  // L59: 没有 break！直接落入 InitSending 分支

        //==================== InitSending / Sending：发送请求 ====================
        case MessageState::InitSending: // fallthrough
        case MessageState::Sending: {//两个 case 共享同一段代码，用 if (state != InitSending) 区分首次和非首次。
            //非首次进入：检查上次 I/O 的结果
            if (state.load() != MessageState::InitSending) {
                fd = request->fd;//从上次的 Request 取回 fd
                if (request->length > 0) {//length > 0 = 实际发送的字节数
                    //发送成功：推进发送偏移
                    sendBufferOffset += request->length;//推进发送偏移
                } else if (request->length != -EINPROGRESS && request->length != -EAGAIN) {
                    //非 EINPROGRESS/EAGAIN 的错误
                    if (request->length == -ECANCELED || request->length == -EINTR) {
                        //超时或被中断：重试
                        failureCode |= static_cast<uint16_t>(MessageFailureCode::Timeout);
                        reset(connectionManager, failures++ > failuresMax);// L75: 重试或放弃
                        continue;// L76: 递归重新执行
                    } else {
                        //其他发送错误：跳到接收阶段（可能服务器已返回错误响应）
                        failureCode |= static_cast<uint16_t>(MessageFailureCode::Send);
                        state.store(MessageState::InitReceiving);// L80: 不重试！直接跳到接收阶段
                        receiveBufferOffset = 0;// L81: 接收偏移归零
                        originalMessage->result.getDataVector().clear();// L82: 清空接收缓冲区
                        continue;
                    }
                }

                //发送完毕？→ 转入接收阶段
                if (sendBufferOffset >= static_cast<int64_t>(originalMessage->message->size() + originalMessage->putLength)) {
                    state.store(MessageState::InitReceiving);// L91: 转入接收阶段
                    receiveBufferOffset = 0;// L92: 接收偏移归零
                    originalMessage->result.getDataVector().clear();// L93: 清空接收缓冲区
                    continue;// L94: 继续执行
                }
            }
            //首次
            state.store(MessageState::Sending);// L96: 无论从 InitSending 还是 Sending 进入，
            {//      统一设为 Sending（下次 execute() 走非首次逻辑）
                //计算本次要发送的数据指针和长度（分块发送）
                const uint8_t* ptr;// L99: 指向本次要发送的数据起始地址
                ptr = originalMessage->message->data() + sendBufferOffset;// L100: 默认从请求头中取数据
                auto length = static_cast<int64_t>(originalMessage->message->size()) - sendBufferOffset;// L101: 默认长度=请求头剩余字节
                //如果请求头已发完，切换到 PUT 数据
                if (originalMessage->putLength > 0 && sendBufferOffset >= static_cast<int64_t>(originalMessage->message->size())) {
                    ptr = originalMessage->putData + sendBufferOffset - originalMessage->message->size();//L104: 计算 PUT 数据的起始地址
                    length = static_cast<int64_t>(originalMessage->putLength + originalMessage->message->size()) - sendBufferOffset;  // 剩余要发送的总字节数
                }
                //构造 Socket::Request 并提交到内核
                request = std::make_unique<Socket::Request>();// L108: 创建新的 Request 对象
                request->data.sendData = ptr; // L109: sendData 指向要发送的数据
                request->length = length; // L110: 要发送的字节数
                request->fd = fd; // L111: 目标 socket 描述符
                request->event = Socket::EventType::write; // L112: 写事件
                request->userData = this;  //complete() 时通过 userData 找回这个 HTTPMessage
                //最后一小块用 send_to（带超时），大块用 send（无超时，靠 keepalive）
                //   complete() 返回 Request* 后，
                //   TaskedSendReceiver 通过 userData 找回这个 HTTPMessage
                //   然后调用 task->execute()
                bool sendOk;
                if (length <= static_cast<int64_t>(chunkSize))
                    sendOk = connectionManager.getSocket().send_to(*request, tcpSettings.timeout);
                else
                    sendOk = connectionManager.getSocket().send(*request);
                if (!sendOk) {
                    // SQ ring full or submission failed — stay in Sending, retry next cycle
                    state.store(MessageState::Sending);
                    break;
                }
            }
            break;// L120: 跳出 switch，返回 state（Sending）
        //      等待事件循环的 complete() 返回结果后再次调用 execute()
        }

        //==================== InitReceiving / Receiving：接收响应 ====================
        case MessageState::InitReceiving: // fallthrough
        case MessageState::Receiving: {
            auto& receive = originalMessage->result.getDataVector();// L126: 引用接收缓冲区//DataVector<uint8_t>，动态字节数组
                                                           //      recv() 数据直接写入这里
            //非首次进入：检查上次接收结果
            if (state.load() != MessageState::InitReceiving) {
                auto reservedLength = receive.size() - static_cast<uint64_t>(receiveBufferOffset);//缓冲区中预分配但还没被实际数据填满的空间
                if (request->length == 0) {
                    //收到 0 字节：连接关闭或对端没数据
                    failureCode |= static_cast<uint16_t>(MessageFailureCode::Empty);
                    reset(connectionManager, failures++ > failuresMax);
                    continue;
                } else if (request->length > 0) {
                    //接收成功：调整缓冲区大小，推进接收偏移
                    receive.resize(static_cast<uint64_t>(receiveBufferOffset) + static_cast<uint64_t>(request->length));//调整缓冲区大小为"已确认数据"的大小
                    /*
                    L137: 调整缓冲区大小为"已确认数据"的大小
                    提交 recv 时 resize(size + chunkSize) 预留了 chunkSize 空间
                    实际收到 request->length 字节（可能 < chunkSize）
                    现在 resize 到精确大小：offset + 实际收到字节数
                    */
                    receiveBufferOffset += request->length;

                    try {
                        //判断 HTTP 响应是否收完;info = 解析结果（首次调用时 info 为空，finished() 内部会调 detect() 解析响应头）
                        if (HttpHelper::finished(receive.data(), static_cast<uint64_t>(receiveBufferOffset), info)) {
                            originalMessage->result.response_ = std::move(info);// L143: 把解析结果存入 MessageResult
                            auto success = HttpResponse::checkSuccess(originalMessage->result.response_->response.code); // 检查 HTTP 状态码是否 2xx
                            //归还连接到池中
                            if (connection_) {
                                connectionManager.returnConnection(std::move(connection_));
                            }
                            if (success) {//注意：无论成功还是失败，只要收到完整响应就归还连接。因为连接本身没坏，可以复用。
                                state.store(MessageState::Finished);  //成功！
                            } else {
                                failureCode |= static_cast<uint16_t>(MessageFailureCode::HTTP);
                                state.store(MessageState::Aborted);   //HTTP 错误（4xx/5xx） // L154: 放弃（不重试 HTTP 错误）
                            }
                            return state.load();
                        }
                    } catch (std::exception&) {
                        //响应解析异常（如格式错误）
                        failureCode |= static_cast<uint16_t>(MessageFailureCode::HTTP);
                        reset(connectionManager, failures++ > failuresMax);
                        continue;
                    }
                } else if (request->length != -EINPROGRESS && request->length != -EAGAIN) {
                    //接收错误
                    if (request->length == -ECANCELED || request->length == -EINTR)
                        failureCode |= static_cast<uint16_t>(MessageFailureCode::Timeout);
                    else
                        failureCode |= static_cast<uint16_t>(MessageFailureCode::Recv);
                    reset(connectionManager, failures++ > failuresMax);
                    continue;
                } else {
                    //EINPROGRESS/EAGAIN：恢复缓冲区大小;//receiveBufferOffset = 已确认数据的末尾;reservedLength = 之前预分配但没用完的空间
                    //resize 恢复到"已确认数据 + 预留空间"，下次 recv 可以继续用
                    receive.resize(static_cast<uint64_t>(receiveBufferOffset) + reservedLength);
                }
                //预分配更多空间（如果已知 Content-Length）
                if (receive.owned() && receive.capacity() < receive.size() + chunkSize && info) {
                    //预分配空间，取两者中较大的：按已知大小精确分配+一点余量; 经典的 1.5 倍扩容策略
                    receive.reserve(std::max(info->length + info->headerLength + chunkSize, receive.capacity() + receive.capacity() / 2));
                }
            }
            //构造 recv Request 并提交到内核
            auto readSize = static_cast<uint64_t>(chunkSize);
            if (!receive.owned()) {
                //外部缓冲区：不能超过容量;上层提供了一个固定大小的 buffer（如 4KB），不能越界写入。
                if (receive.capacity() <= receiveBufferOffset) {
                    failureCode |= static_cast<uint16_t>(MessageFailureCode::Recv);
                    state.store(MessageState::Aborted);
                    return state.load();
                }
                readSize = std::min<uint64_t>(chunkSize, receive.capacity() - receiveBufferOffset);// L190: 取较小值
                                                                                          //   不超过缓冲区剩余容量
            }
            receive.resize(receive.size() + readSize);// L192: 预留 readSize 字节空间
                                             //      resize 后 size 变大，但实际数据还没到
                                             //      [已确认数据 | 预留空间(readSize)]
            request = std::make_unique<Socket::Request>();
            request->data.recvData = receive.data() + receiveBufferOffset;
            request->length = static_cast<int64_t>(readSize); // L195: 最多接收 readSize 字节
            request->fd = connection_ ? connection_->getSocket() : fd;// L196: 优先用 connection_ 的 fd//fallback 到局部 fd（重试场景 connection_ 可能为空）
            request->event = Socket::EventType::read;
            request->userData = this;// L198: 指向自己，complete() 时找回
            auto recvFlags = tcpSettings.recvNoWait > 0 ? MSG_DONTWAIT : 0;
            if (!connectionManager.getSocket().recv_to(*request, tcpSettings.timeout, recvFlags)) {
                // recv submission failed — keep current state, retry next cycle
                break;
            }
            state.store(MessageState::Receiving);
            break;
        }
        default:
            break;
        }
        return state.load();  // normal completion (or Aborted via default)
    }
}

//重置状态机，准备重试或放弃
//aborted=false: 清空缓冲区，回到 Init 状态重试
//aborted=true:  直接进入 Aborted 状态，不再重试
void HTTPMessage::reset(ConnectionManager& connectionManager, bool aborted) {
    if (!aborted) {
        //重试：清空所有进度，从 Init 重新开始
        originalMessage->result.getDataVector().clear();
        receiveBufferOffset = 0;
        sendBufferOffset = 0;
        info.reset();
        originalMessage->result.state_ = MessageState::Init;
    } else {
        //放弃：直接标记为 Aborted
        originalMessage->result.state_ = MessageState::Aborted;
    }
    //如果是 HTTP 错误且 Provider 支持重签名（如 S3 签名过期），重新签名
    if ((originalMessage->result.failureCode_ & static_cast<uint16_t>(MessageFailureCode::HTTP)) && originalMessage->provider.supportsResigning()) {
        originalMessage->message = originalMessage->provider.resignRequest(*originalMessage->message, originalMessage->putData, originalMessage->putLength);
    }
    //断开并归还连接（连接可能已损坏，不复用）
    if (connection_) {
        connection_->disconnect();
        connectionManager.returnConnection(std::move(connection_));
    }
    if (request) {
        request->fd = -1;  //标记请求无效
    }
}

} // namespace myblob::network

#pragma once
#include "../utils/data_vector.hpp"
#include "http_helper.hpp"
#include "message_state.hpp"
#include <atomic>
#include <memory>
#include <string_view>

namespace myblob::network {

struct OriginalMessage;
class Transaction;

class MessageResult {
public:
    MessageResult();
    
    explicit MessageResult(uint8_t* data, uint64_t size);
    
    explicit MessageResult(utils::DataVector<uint8_t>* dataVector);
    
    [[nodiscard]] std::string_view getResult() const;
    [[nodiscard]] std::string_view getResult();
    [[nodiscard]] const uint8_t* getData() const;
    [[nodiscard]] uint8_t* getData();//原始字节指针
    [[nodiscard]] std::unique_ptr<uint8_t[]> moveData();
    [[nodiscard]] uint64_t getSize() const;
    [[nodiscard]] uint64_t getOffset() const;//body起始偏移
    [[nodiscard]] MessageState getState() const;
    [[nodiscard]] uint16_t getFailureCode() const;
    [[nodiscard]] std::string_view getErrorResponse() const;
    [[nodiscard]] std::string_view getResponseCode() const;
    [[nodiscard]] uint64_t getResponseCodeNumber() const;
    [[nodiscard]] bool owned() const;
    [[nodiscard]] bool success() const;
    [[nodiscard]] const MessageResult* getOriginError() const { return originError_; }
    [[nodiscard]] utils::DataVector<uint8_t>& getDataVector();//dataVector_ 引用（状态机用来 recv 写入）
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> moveDataVector();//转移所有权
    
    void setState(MessageState state) { state_.store(state); }
    void setFailureCode(uint16_t code) { failureCode_ = code; }
    void setOriginError(const MessageResult* originError) { originError_ = originError; }

protected:
    friend struct HTTPMessage;
    friend struct HTTPSMessage;
    friend struct MessageTask;
    std::unique_ptr<utils::DataVector<uint8_t>> dataVector_;//原始http响应字节流(http头+body)
    std::unique_ptr<HttpHelper::Info> response_;//解析后的http响应元信息
    const MessageResult* originError_{nullptr};//多部分上传指向错误来源
    uint16_t failureCode_{0};//失败码位掩码
    std::atomic<MessageState> state_{MessageState::Init};//当前状态
    
    friend OriginalMessage;
    friend Transaction;
};

}  // namespace myblob::network

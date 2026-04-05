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
    [[nodiscard]] uint8_t* getData();
    [[nodiscard]] std::unique_ptr<uint8_t[]> moveData();
    [[nodiscard]] uint64_t getSize() const;
    [[nodiscard]] uint64_t getOffset() const;
    [[nodiscard]] MessageState getState() const;
    [[nodiscard]] uint16_t getFailureCode() const;
    [[nodiscard]] std::string_view getErrorResponse() const;
    [[nodiscard]] std::string_view getResponseCode() const;
    [[nodiscard]] uint64_t getResponseCodeNumber() const;
    [[nodiscard]] bool owned() const;
    [[nodiscard]] bool success() const;
    [[nodiscard]] utils::DataVector<uint8_t>& getDataVector();
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> moveDataVector();

protected:
    std::unique_ptr<utils::DataVector<uint8_t>> dataVector_;
    std::unique_ptr<HttpHelper::Info> response_;
    const MessageResult* originError_{nullptr};
    uint16_t failureCode_{0};
    std::atomic<MessageState> state_{MessageState::Init};
    
    friend OriginalMessage;
    friend Transaction;
};

}  // namespace myblob::network

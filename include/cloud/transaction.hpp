#pragma once
#include "../network/message_result.hpp"
#include "../network/original_message.hpp"
#include "provider.hpp"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace myblob::cloud {

class Transaction {
public:
    using message_vector_type = std::vector<std::unique_ptr<network::OriginalMessage>>;
    
    Transaction();
    
    explicit Transaction(Provider* provider);
    
    void setProvider(Provider* provider);
    
    bool getObjectRequest(
        const std::string& remotePath,
        std::pair<uint64_t, uint64_t> range = {0, 0},
        uint8_t* result = nullptr,
        uint64_t capacity = 0
    );
    
    template <typename Callback>
    bool getObjectRequest(
        Callback&& callback,
        const std::string& remotePath,
        std::pair<uint64_t, uint64_t> range = {0, 0},
        uint8_t* result = nullptr,
        uint64_t capacity = 0
    );
    
    bool putObjectRequest(
        const std::string& remotePath,
        const char* data,
        uint64_t size,
        uint8_t* result = nullptr,
        uint64_t capacity = 0
    );
    
    template <typename Callback>
    bool putObjectRequest(
        Callback&& callback,
        const std::string& remotePath,
        const char* data,
        uint64_t size,
        uint8_t* result = nullptr,
        uint64_t capacity = 0
    );
    
    bool deleteObjectRequest(
        const std::string& remotePath,
        uint8_t* result = nullptr,
        uint64_t capacity = 0
    );
    
    template <typename Callback>
    bool deleteObjectRequest(
        Callback&& callback,
        const std::string& remotePath,
        uint8_t* result = nullptr,
        uint64_t capacity = 0
    );
    
    message_vector_type& getMessages() { return messages_; }
    
    const message_vector_type& getMessages() const { return messages_; }

private:
    Provider* provider_{nullptr};
    message_vector_type messages_;
};

template <typename Callback>
bool Transaction::getObjectRequest(
    Callback&& callback,
    const std::string& remotePath,
    std::pair<uint64_t, uint64_t> range,
    uint8_t* result,
    uint64_t capacity
) {
    if (!provider_) return false;
    
    auto msg = std::make_unique<network::OriginalCallbackMessage<Callback>>(
        std::forward<Callback>(callback),
        provider_->getRequest(remotePath, range),
        *provider_,
        result,
        capacity
    );
    
    messages_.push_back(std::move(msg));
    return true;
}

template <typename Callback>
bool Transaction::putObjectRequest(
    Callback&& callback,
    const std::string& remotePath,
    const char* data,
    uint64_t size,
    uint8_t* result,
    uint64_t capacity
) {
    if (!provider_) return false;
    
    auto object = std::string_view(data, size);
    auto msg = std::make_unique<network::OriginalCallbackMessage<Callback>>(
        std::forward<Callback>(callback),
        provider_->putRequest(remotePath, object),
        *provider_,
        result,
        capacity
    );
    
    msg->setPutRequestData(reinterpret_cast<const uint8_t*>(data), size);
    messages_.push_back(std::move(msg));
    return true;
}

template <typename Callback>
bool Transaction::deleteObjectRequest(
    Callback&& callback,
    const std::string& remotePath,
    uint8_t* result,
    uint64_t capacity
) {
    if (!provider_) return false;
    
    auto msg = std::make_unique<network::OriginalCallbackMessage<Callback>>(
        std::forward<Callback>(callback),
        provider_->deleteRequest(remotePath),
        *provider_,
        result,
        capacity
    );
    
    messages_.push_back(std::move(msg));
    return true;
}

}  // namespace myblob::cloud

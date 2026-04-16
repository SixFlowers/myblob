#pragma once
#include "cloud_service.hpp"
#include "provider.hpp"
#include "../network/original_message.hpp"
#include "../network/connection_manager.hpp"
#include "../network/socket.hpp"
#include "../network/tasked_send_receiver.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <atomic>

namespace myblob::cloud {

// 事务类用于处理云存储操作
class Transaction {
public:
    using message_vector_type = std::vector<std::unique_ptr<network::OriginalMessage>>;
    
    // 多部分上传状态管理
    struct MultipartUpload {
        enum State : uint8_t {
            Default = 0,
            Sending = 1,
            Processing = 2,
            Validating = 3,
            Aborted = 1u << 7
        };
        
        std::string uploadId;
        message_vector_type messages;
        std::vector<std::string> eTags;
        std::atomic<int> outstanding;
        std::atomic<State> state;
        std::atomic<uint64_t> errorMessageId;

        explicit MultipartUpload(uint16_t parts)
            : messages(parts + 1)
            , eTags(parts)
            , outstanding(static_cast<int>(parts))
            , state(State::Default) {}

        MultipartUpload(MultipartUpload&& other) noexcept
            : uploadId(std::move(other.uploadId))
            , messages(std::move(other.messages))
            , eTags(std::move(other.eTags))
            , outstanding(other.outstanding.load())
            , state(other.state.load()) {}
    };

    // 迭代器定义
    class Iterator;
    class ConstIterator;

    Transaction();
    explicit Transaction(Provider* provider);
    
    void setProvider(Provider* provider);
    
    // 同步处理
    void processSync(network::TaskedSendReceiverHandle& sendReceiverHandle);
    
    // 异步处理
    bool processAsync(network::TaskedSendReceiverGroup& group);
    
    // 密钥验证
    template <typename Function>
    bool verifyKeyRequest(network::TaskedSendReceiverHandle& sendReceiverHandle,
                          Function&& func) {
        assert(provider_);
        provider_->initSecret(sendReceiverHandle);
        return std::forward<Function>(func)();
    }
    
    // GET请求
    bool getObjectRequest(const std::string& remotePath,
                          std::pair<uint64_t, uint64_t> range = {0, 0},
                          uint8_t* result = nullptr,
                          uint64_t capacity = 0,
                          uint64_t traceId = 0);
    
    template <typename Callback>
    bool getObjectRequest(Callback&& callback,
                          const std::string& remotePath,
                          std::pair<uint64_t, uint64_t> range = {0, 0},
                          uint8_t* result = nullptr,
                          uint64_t capacity = 0,
                          uint64_t traceId = 0);
    
    // PUT请求
    bool putObjectRequest(const std::string& remotePath,
                          const char* data,
                          uint64_t size,
                          uint8_t* result = nullptr,
                          uint64_t capacity = 0,
                          uint64_t traceId = 0);
    
    template <typename Callback>
    bool putObjectRequest(Callback&& callback,
                          const std::string& remotePath,
                          const char* data,
                          uint64_t size,
                          uint8_t* result = nullptr,
                          uint64_t capacity = 0,
                          uint64_t traceId = 0);
    
    // DELETE请求
    bool deleteObjectRequest(const std::string& remotePath,
                             uint8_t* result = nullptr,
                             uint64_t capacity = 0,
                             uint64_t traceId = 0);
    
    template <typename Callback>
    bool deleteObjectRequest(Callback&& callback,
                             const std::string& remotePath,
                             uint8_t* result = nullptr,
                             uint64_t capacity = 0,
                             uint64_t traceId = 0);
    
    // 执行所有请求（旧接口，保留兼容）
    void execute();
    
    // 异步执行（旧接口，保留兼容）
    void executeAsync();
    
    // 异步执行带回调（旧接口，保留兼容）
    template<typename Callback>
    void executeAsync(Callback&& callback) {
        std::thread([this, callback]() {
            execute();
            callback();
        }).detach();
    }
    
    // 获取消息列表
    const std::vector<std::unique_ptr<network::OriginalMessage>>& getMessages() const {
        return messages_;
    }
    
    // 迭代器接口
    Iterator begin();
    Iterator end();
    ConstIterator cbegin() const;
    ConstIterator cend() const;

private:
    Provider* provider_;
    message_vector_type messages_;
    std::atomic<uint64_t> messageCounter_{0};
    std::vector<MultipartUpload> multipartUploads_;
    std::atomic<uint64_t> completedMultiparts_{0};
    std::unique_ptr<network::ConnectionManager> connMgr_;
    std::vector<std::unique_ptr<network::Socket::Request>> requestHolder_;
    
    template <typename Callback, typename... Arguments>
    std::unique_ptr<network::OriginalCallbackMessage<Callback>> 
    makeCallbackMessage(Callback&& c, Arguments&&... args) {
        return std::make_unique<network::OriginalCallbackMessage<Callback>>(
            std::forward<Callback>(c),
            std::forward<Arguments>(args)...);
    }
    
    // 多部分上传
    template <typename Callback>
    bool putObjectRequestMultiPart(Callback&& callback,
                                   const std::string& remotePath,
                                   const char* data,
                                   uint64_t size,
                                   uint8_t* result = nullptr,
                                   uint64_t capacity = 0,
                                   uint64_t traceId = 0);
};

// Iterator 实现
class Transaction::Iterator {
public:
    using value_type = network::MessageResult;
    using difference_type = std::ptrdiff_t;
    using reference = value_type&;
    using pointer = value_type*;
    using iterator_category = std::forward_iterator_tag;

    explicit Iterator(message_vector_type::iterator it) : it_(it) {}
    
    reference operator*() const { return (*it_)->result; }
    pointer operator->() const { return &(*it_)->result; }
    
    Iterator& operator++() {
        ++it_;
        return *this;
    }
    
    Iterator operator++(int) {
        Iterator prv(*this);
        operator++();
        return prv;
    }
    
    bool operator==(const Iterator& other) const { return it_ == other.it_; }
    bool operator!=(const Iterator& other) const { return it_ != other.it_; }

private:
    message_vector_type::iterator it_;
    friend Transaction;
};

class Transaction::ConstIterator {
public:
    using value_type = const network::MessageResult;
    using difference_type = std::ptrdiff_t;
    using reference = value_type&;
    using pointer = value_type*;
    using iterator_category = std::forward_iterator_tag;

    explicit ConstIterator(message_vector_type::const_iterator it) : it_(it) {}
    
    reference operator*() const { return (*it_)->result; }
    pointer operator->() const { return &(*it_)->result; }
    
    ConstIterator& operator++() {
        ++it_;
        return *this;
    }
    
    ConstIterator operator++(int) {
        ConstIterator prv(*this);
        operator++();
        return prv;
    }
    
    bool operator==(const ConstIterator& other) const { 
        return it_ == other.it_; 
    }
    bool operator!=(const ConstIterator& other) const { 
        return it_ != other.it_; 
    }

private:
    message_vector_type::const_iterator it_;
    friend Transaction;
};

} // namespace myblob::cloud

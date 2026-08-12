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
            Validating = 3,//发送"完全上传"请求前设置
            Aborted = 1u << 7
        };
        
        std::string uploadId;//上传的会话ID
        message_vector_type messages;//所有分片的消息
        std::vector<std::string> eTags;//每个分片的校验码
        std::atomic<int> outstanding;//未完成的分片数
        std::atomic<State> state;//当前状态
        std::atomic<uint64_t> errorMessageId;//错误分片ID

        explicit MultipartUpload(uint16_t parts)
            : messages(parts + 1)//// +1是因为第一个消息是Initiate请求
            , eTags(parts)
            , outstanding(static_cast<int>(parts))
            , state(State::Default)
            , errorMessageId(0) {}

        MultipartUpload(MultipartUpload&& other) noexcept
            : uploadId(std::move(other.uploadId))
            , messages(std::move(other.messages))
            , eTags(std::move(other.eTags))
            , outstanding(other.outstanding.load())
            , state(other.state.load())
            , errorMessageId(other.errorMessageId.load()) {}
    };

    // 迭代器定义
    class Iterator;
    class ConstIterator;

    Transaction();
    explicit Transaction(Provider* provider);//explicit:必须显式调用
    
    void setProvider(Provider* provider);
    
    // 同步处理
    void execute();
    void processSync(network::TaskedSendReceiverHandle& sendReceiverHandle);
    
    // 异步处理
    bool processAsync(network::TaskedSendReceiverGroup& group);
    
    // 密钥验证
    template <typename Function>
    bool verifyKeyRequest(network::TaskedSendReceiverHandle& sendReceiverHandle,
                          Function&& func) {
        assert(provider_);
        provider_->initSecret(sendReceiverHandle);
        return std::forward<Function>(func)();//std::forward(保持参数的"值类别"[左值/右值])
    }
    //以下请求创建provider请求并加入到messages_容器
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
                          uint64_t traceId = 0) {
        if (!provider_) {
            return false;
        }
        auto request = provider_->getRequest(remotePath, range);
        if (!request) {
            return false;
        }
        auto msg = makeCallbackMessage(
            std::forward<Callback>(callback),
            std::move(request),
            *provider_,
            result,
            capacity,
            traceId
        );
        messages_.push_back(std::move(msg));
        return true;
    }
    
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
                          uint64_t traceId = 0) {
        if (!provider_) {
            return false;
        }
        if (provider_->multipartUploadSize() > 0 && size > provider_->multipartUploadSize()) {
            return putObjectRequestMultiPart(
                std::forward<Callback>(callback),
                remotePath,
                data,
                size,
                result,
                capacity,
                traceId
            );
        }
        auto request = provider_->putRequest(remotePath, std::string_view(data, size));
        if (!request) {
            return false;
        }
        auto msg = makeCallbackMessage(
            std::forward<Callback>(callback),
            std::move(request),
            *provider_,
            result,
            capacity,
            traceId
        );
        msg->setPutRequestData(reinterpret_cast<const uint8_t*>(data), size);
        messages_.push_back(std::move(msg));
        return true;
    }
    
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
                             uint64_t traceId = 0) {
        if (!provider_) {
            return false;
        }
        auto request = provider_->deleteRequest(remotePath);
        if (!request) {
            return false;
        }
        auto msg = makeCallbackMessage(
            std::forward<Callback>(callback),
            std::move(request),
            *provider_,
            result,
            capacity,
            traceId
        );
        messages_.push_back(std::move(msg));
        return true;
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
    message_vector_type messages_;//provider的请求消息容器
    std::vector<MultipartUpload> multipartUploads_;//分片上传的任务容器(专门struct类)
    std::atomic<uint64_t> completedMultiparts_{0};//已完成的多部分上传任务数
    std::atomic<uint64_t> messageCounter_{0};
    std::unique_ptr<network::ConnectionManager> connMgr_;//连接池

    //这是一个 辅助函数模板 ，用于创建带回调的消息对象。
    template <typename Callback, typename... Arguments>
    std::unique_ptr<network::OriginalCallbackMessage<Callback>> 
    makeCallbackMessage(Callback&& c, Arguments&&... args/*自定义数量类型传参*/) {
        return std::make_unique<network::OriginalCallbackMessage<Callback>>(
            std::forward<Callback>(c),
            std::forward<Arguments>(args)...);
    }
    
    // 多部分上传实现
    template <typename Callback>
    bool putObjectRequestMultiPart(Callback&& callback,
                                   const std::string& remotePath,
                                   const char* data,
                                   uint64_t size,
                                   uint8_t* result = nullptr,
                                   uint64_t capacity = 0,
                                   uint64_t traceId = 0) {
        assert(provider_);
        
        auto splitSize = provider_->multipartUploadSize();
        uint16_t parts = static_cast<uint16_t>(
            (size / splitSize) + ((size % splitSize) ? 1u : 0u));
        
        multipartUploads_.emplace_back(parts);//分片上传总任务；parts通过构造函数构造
        auto position = multipartUploads_.size() - 1;
        
        auto uploadMessages = [callback = std::forward<Callback>(callback),
                              position, parts, data, remotePath, traceId,
                              splitSize, size, this](
                                 network::MessageResult& initialRequestResult/*初始化上传请求的结果*/) {
            if (!initialRequestResult.success()) {
                completedMultiparts_++;
                callback(initialRequestResult);
                return;
            }
            
            provider_->getSecret();//获取/验证密钥
            multipartUploads_[position].uploadId = provider_->getUploadId(//uploadid是任务id，不是分片id
                initialRequestResult.getResult());
            
            auto offset = 0ull;
            //分片上传完成后的结果回调函数
            for (uint16_t i = 1; i <= parts; i++) {
                auto finishMultipart = [&callback, &initialRequestResult, position,
                                        remotePath, traceId, i, parts, this](
                                           network::MessageResult& result) {
                    provider_->getSecret();
                    
                    if (!result.success()) {
                        multipartUploads_[position].errorMessageId = i - 1;//失败的分片id
                        multipartUploads_[position].state = 
                        MultipartUpload::State::Aborted;//不立即返回是为了完成其它分片同一清理
                    } else {
                        multipartUploads_[position].eTags[i - 1] = provider_->getETag(//获取当前分片校验码(从http响应头的整个数据中)
                            std::string_view(//string_view实现0拷贝
                                reinterpret_cast<const char*>(result.getData()),
                                result.getOffset())/*响应的数据(数据指针+长度)*/);
                    }
                    
                    if (multipartUploads_[position].outstanding.fetch_sub(1) == 1/*原子版的a--;判断是否是最后一次上传*/) {
                        if (multipartUploads_[position].state != 
                            MultipartUpload::State::Aborted) {
                            // 完成多部分上传
                            auto contentData = std::make_unique<std::string>();
                            auto content = contentData.get();
                            auto finished = [&callback, &initialRequestResult,
                                            contentPtr = std::move(contentData)/*按值捕获的变量 */,
                                            this](network::MessageResult& result) mutable/**mutable:允许修改按值捕获的变量 */ {
                                if (!result.success()) {
                                    initialRequestResult.setState(
                                        network::MessageState::Cancelled);//合并分片失败(被系统/服务器取消)
                                    initialRequestResult.setOriginError(&result);//捕获问题来源
                                }
                                completedMultiparts_++;
                                callback(initialRequestResult);
                            };
                            //注册完成多部份上传的回调函数
                            auto originalMsg = makeCallbackMessage(
                                std::move(finished),
                                provider_->completeMultiPartRequest(
                                    remotePath,
                                    multipartUploads_[position].uploadId,
                                    multipartUploads_[position].eTags,
                                    *content),
                                *provider_,
                                nullptr,
                                0,
                                traceId
                            );
                            
                            originalMsg->setPutRequestData(
                                reinterpret_cast<const uint8_t*>(content->data()),
                                content->size());
                            
                            multipartUploads_[position].messages[parts] = 
                                std::move(originalMsg);
                        } else {
                            // 中止上传
                            auto finished = [&callback, &initialRequestResult,
                                            position, this](
                                               network::MessageResult& /*result*/) {
                                initialRequestResult.setState(
                                    network::MessageState::Cancelled);
                                initialRequestResult.setOriginError(
                                    &multipartUploads_[position].messages[
                                        multipartUploads_[position].errorMessageId]->result);
                                completedMultiparts_++;
                                callback(initialRequestResult);
                            };
                            //注册中止上传的回调函数
                            auto originalMsg = makeCallbackMessage(
                                std::move(finished),
                                provider_->deleteRequestGeneric(
                                    remotePath,
                                    multipartUploads_[position].uploadId),
                                *provider_,
                                nullptr,
                                0,
                                traceId
                            );
                            
                            multipartUploads_[position].messages[parts] = 
                                std::move(originalMsg);
                        }
                        multipartUploads_[position].state = 
                            MultipartUpload::State::Validating;
                    }
                };
                
                auto partSize = (i != parts) ? splitSize : size - offset;
                auto object = std::string_view(data + offset, partSize);
                //注册finishMultipart回调函数
                auto originalMsg = makeCallbackMessage(
                    std::move(finishMultipart),
                    provider_->putRequestGeneric(
                        remotePath, object, i,
                        multipartUploads_[position].uploadId),
                    *provider_,
                    nullptr,
                    0,
                    traceId
                );
                
                originalMsg->setPutRequestData(
                    reinterpret_cast<const uint8_t*>(data + offset), partSize);
                
                multipartUploads_[position].messages[i - 1] = std::move(originalMsg);
                offset += partSize;
            }
            
            multipartUploads_[position].state = MultipartUpload::State::Sending;
        };
        //分片上传任务请求，注册回调函数,第一个回调函数,其中包含了每个分片上传的回调
        auto originalMsg = makeCallbackMessage(
            std::move(uploadMessages),
            provider_->createMultiPartRequest(remotePath),
            *provider_,
            result,
            capacity,
            traceId
        );
        
        if (!originalMsg)
            return false;
        
        messages_.push_back(std::move(originalMsg));//sendReceiver.sendSync(msg) 
        return true;
    }
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

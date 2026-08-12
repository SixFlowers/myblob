#pragma once
#include "../utils/data_vector.hpp"
#include "message_result.hpp"
#include <cstdint>
#include <memory>
#include <utility>

namespace myblob::cloud {
class Provider;
}  // namespace myblob::cloud

namespace myblob::network {

struct OriginalMessage {
    //用uint8_t(0-255)是表示每个字符而不是长度;字符串存储在datavector里
    std::unique_ptr<utils::DataVector<uint8_t>> message;//HTTP 请求头（已签名的字节流）
    cloud::Provider& provider;//目标云提供商
    MessageResult result;//响应结果（状态、数据、错误信息）
    
    const uint8_t* putData{nullptr};//PUT 上传时真正要发送的请求体数据（与 message 分离，因为请求头里只有元数据）
    uint64_t putLength{0};
    uint64_t traceId{0};//追踪 ID，供性能计时使用
    
    OriginalMessage(
        std::unique_ptr<utils::DataVector<uint8_t>> msg,
        cloud::Provider& provider,
        uint8_t* receiveBuffer = nullptr,
        uint64_t bufferSize = 0, 
        uint64_t trace = 0
    );
    
    virtual ~OriginalMessage() = default;
    
    virtual bool requiresFinish() { return false; }//调度器判断你是否需要回调(主要给OriginalCallbackMessage用)
    
    virtual void finish() {}
    
    void setPutRequestData(const uint8_t* data, uint64_t length) {
        this->putData = data;
        this->putLength = length;
    }
    
    void setResultVector(utils::DataVector<uint8_t>* dataVector) {
        result.dataVector_ = std::unique_ptr<utils::DataVector<uint8_t>>(dataVector);
    }
};

template <typename Callback>
struct OriginalCallbackMessage : public OriginalMessage {
    Callback callback;
    
    OriginalCallbackMessage(
        Callback&& cb,
        std::unique_ptr<utils::DataVector<uint8_t>> msg,
        cloud::Provider& provider,
        uint8_t* receiveBuffer = nullptr,
        uint64_t bufferSize = 0,
        uint64_t trace = 0
    ) : OriginalMessage(std::move(msg), provider, receiveBuffer, bufferSize, trace),
        callback(std::forward<Callback>(cb)) {}
    
    bool requiresFinish() override { return true; }
    
    void finish() override {
        callback(result);
    }
};

}  // namespace myblob::network

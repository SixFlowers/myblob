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
    std::unique_ptr<utils::DataVector<uint8_t>> message;
    cloud::Provider& provider;
    MessageResult result;
    
    const uint8_t* putData{nullptr};
    uint64_t putLength{0};
    uint64_t traceId{0};
    
    OriginalMessage(
        std::unique_ptr<utils::DataVector<uint8_t>> msg,
        cloud::Provider& provider,
        uint8_t* receiveBuffer = nullptr,
        uint64_t bufferSize = 0,
        uint64_t trace = 0
    );
    
    virtual ~OriginalMessage() = default;
    
    virtual bool requiresFinish() { return false; }
    
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

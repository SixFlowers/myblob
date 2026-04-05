#include "cloud/transaction.hpp"

namespace myblob::cloud {

Transaction::Transaction()
    : provider_(nullptr) {
}

Transaction::Transaction(Provider* provider)
    : provider_(provider) {
}

void Transaction::setProvider(Provider* provider) {
    provider_ = provider;
}

bool Transaction::getObjectRequest(
    const std::string& remotePath,
    std::pair<uint64_t, uint64_t> range,
    uint8_t* result,
    uint64_t capacity
) {
    if (!provider_) {
        return false;
    }
    
    auto msg = std::make_unique<network::OriginalMessage>(
        provider_->getRequest(remotePath, range),
        *provider_,
        result,
        capacity
    );
    
    messages_.push_back(std::move(msg));
    return true;
}

bool Transaction::putObjectRequest(
    const std::string& remotePath,
    const char* data,
    uint64_t size,
    uint8_t* result,
    uint64_t capacity
) {
    if (!provider_) return false;
    
    auto object = std::string_view(data, size);
    auto msg = std::make_unique<network::OriginalMessage>(
        provider_->putRequest(remotePath, object),
        *provider_,
        result,
        capacity
    );
    
    msg->setPutRequestData(reinterpret_cast<const uint8_t*>(data), size);
    messages_.push_back(std::move(msg));
    return true;
}

bool Transaction::deleteObjectRequest(
    const std::string& remotePath,
    uint8_t* result,
    uint64_t capacity
) {
    if (!provider_) return false;
    
    auto msg = std::make_unique<network::OriginalMessage>(
        provider_->deleteRequest(remotePath),
        *provider_,
        result,
        capacity
    );
    
    messages_.push_back(std::move(msg));
    return true;
}

}  // namespace myblob::cloud

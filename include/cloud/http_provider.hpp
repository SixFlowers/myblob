#pragma once
#include "cloud_service.hpp"
#include "../network/connection_manager.hpp"
#include "../network/http_client.hpp"
#include "../utils/data_vector.hpp"
#include <memory>
#include <string>
#include "provider.hpp"

namespace myblob::cloud {

class HTTPProvider : public Provider {
public:
    HTTPProvider(const std::string& addr, uint16_t port, bool https,
                 myblob::network::ConnectionManager& conn_mgr,
                 myblob::network::HttpClient& http_client);
    
    std::unique_ptr<utils::DataVector<uint8_t>> getRequest(
        const std::string& filePath,
        const std::pair<uint64_t, uint64_t>& range = {0, 0}
    ) const override;
    
    std::unique_ptr<utils::DataVector<uint8_t>> putRequest(
        const std::string& filePath,
        std::string_view object
    ) const override;
    
    std::unique_ptr<utils::DataVector<uint8_t>> deleteRequest(
        const std::string& filePath
    ) const override;

    // 实现AWS需要的额外虚函数（HTTPProvider不支持多部分上传）
    std::unique_ptr<utils::DataVector<uint8_t>> putRequestGeneric(
        const std::string& filePath,
        std::string_view object,
        uint16_t part,
        std::string_view uploadId
    ) const override;
    
    std::unique_ptr<utils::DataVector<uint8_t>> createMultiPartRequest(
        const std::string& filePath
    ) const override;
    
    std::unique_ptr<utils::DataVector<uint8_t>> completeMultiPartRequest(
        const std::string& filePath,
        std::string_view uploadId,
        const std::vector<std::string>& etags,
        std::string& content
    ) const override;
    
    std::unique_ptr<utils::DataVector<uint8_t>> resignRequest(
        const utils::DataVector<uint8_t>& data,
        const uint8_t* bodyData = nullptr,
        uint64_t bodyLength = 0
    ) const override;
    
    uint64_t multipartUploadSize() const override;
    
    Instance getInstanceDetails(myblob::network::TaskedSendReceiverHandle& sendReceiver) override;
    
    void initCache(myblob::network::TaskedSendReceiverHandle& sendReceiverHandle) override;
    
    std::string getAddress() const override { return address_; }
    
    uint16_t getPort() const override { return port_; }
    
    CloudService getType() const override { return type_; }

protected:
    void initSecret(myblob::network::TaskedSendReceiverHandle& sendReceiverHandle) override;
    void getSecret() override;
};

}  // namespace myblob::cloud

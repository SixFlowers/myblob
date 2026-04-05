#pragma once
#include "cloud_service.hpp"
#include "../network/http_client.hpp"
#include "../utils/data_vector.hpp"
#include <memory>
#include <string>
#include "provider.hpp"

namespace myblob::cloud {

class HTTPProvider : public Provider {
public:
    HTTPProvider(const std::string& addr, uint16_t port, bool https,
                 network::ConnectionManager& conn_mgr,
                 network::HttpClient& http_client);
    
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
    
    std::string getAddress() const override { return address_; }
    
    uint16_t getPort() const override { return port_; }
    
    CloudService getType() const override { return type_; }
};

}  // namespace myblob::cloud

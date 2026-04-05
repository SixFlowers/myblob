#pragma once
#include "cloud_service.hpp"
#include "../network/http_client.hpp"
#include "../network/connection_manager.hpp"
#include "../utils/data_vector.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace myblob::cloud {

class Provider {
public:
    static constexpr const char* remoteFile[] = {
        "https://",
        "http://",
        "s3://",
        "azure://",
        "gs://",
        "oci://",
        "ibm://",
        "minio://"
    };
    
    static constexpr size_t remoteFileCount = 8;
    
    explicit Provider(network::ConnectionManager& conn_mgr,
                      network::HttpClient& http_client,
                      CloudService type = CloudService::HTTPS);
    
    Provider(const std::string& addr, uint16_t port, 
             network::ConnectionManager& conn_mgr,
             network::HttpClient& http_client,
             CloudService type);
    
    virtual ~Provider() = default;
    
    static std::unique_ptr<Provider> makeProvider(
        const std::string& url,
        bool https = false,
        const std::string& access_key = "",
        const std::string& secret_key = "",
        void* send_receiver_handle = nullptr
    );
    
    static bool isRemoteFile(const std::string& url);
    
    static CloudService getCloudService(const std::string& url);
    
    virtual std::unique_ptr<utils::DataVector<uint8_t>> getRequest(
        const std::string& filePath,
        const std::pair<uint64_t, uint64_t>& range = {0, 0}
    ) const = 0;
    virtual std::unique_ptr<utils::DataVector<uint8_t>> putRequest(
        const std::string& filePath,
        std::string_view object
    ) const = 0;
    
    virtual std::unique_ptr<utils::DataVector<uint8_t>> deleteRequest(
        const std::string& filePath
    ) const = 0;
    
    virtual std::string getAddress() const { return address_; }
    
    virtual uint16_t getPort() const { return port_; }
    
    virtual CloudService getType() const { return type_; }
    
    bool isHTTPS() const { return type_ == CloudService::HTTPS; }
    
    network::HttpResponse download(
        const std::string& file_path,
        uint64_t offset = 0,
        uint64_t length = 0
    );
    
protected:
    network::ConnectionManager* conn_mgr_ = nullptr;
    network::HttpClient* http_client_ = nullptr;
    std::string address_;
    uint16_t port_;
    CloudService type_;
};

}  // namespace myblob::cloud

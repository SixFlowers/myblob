#include "cloud/provider.hpp"
#include "cloud/http_provider.hpp"
#include <iostream>

namespace myblob::cloud {

Provider::Provider(network::ConnectionManager& conn_mgr,
                   network::HttpClient& http_client,
                   CloudService type)
    : conn_mgr_(&conn_mgr)
    , http_client_(&http_client)
    , type_(type) {
    std::cerr << "[DEBUG] Provider: Created with ConnectionManager, type=" 
              << static_cast<int>(type_) << std::endl;
}

Provider::Provider(const std::string& addr, uint16_t port,
                   network::ConnectionManager& conn_mgr,
                   network::HttpClient& http_client,
                   CloudService type)
    : conn_mgr_(&conn_mgr)
    , http_client_(&http_client)
    , address_(addr)
    , port_(port)
    , type_(type) {
    std::cerr << "[DEBUG] Provider: Created with addr/port, type=" 
              << static_cast<int>(type_) << std::endl;
}

bool Provider::isRemoteFile(const std::string& url) {
    for (size_t i = 0; i < remoteFileCount; i++) {
        if (url.find(remoteFile[i]) == 0) {
            return true;
        }
    }
    return false;
}

CloudService Provider::getCloudService(const std::string& url) {
    if (url.find("https://") == 0) return CloudService::HTTPS;
    if (url.find("http://") == 0) return CloudService::HTTP;
    if (url.find("s3://") == 0) return CloudService::AWS;
    if (url.find("azure://") == 0) return CloudService::Azure;
    if (url.find("gs://") == 0) return CloudService::GCP;
    if (url.find("oci://") == 0) return CloudService::Oracle;
    if (url.find("ibm://") == 0) return CloudService::IBM;
    if (url.find("minio://") == 0) return CloudService::MinIO;
    return CloudService::Local;
}

std::unique_ptr<Provider> Provider::makeProvider(
    const std::string& url,
    bool https,
    const std::string& access_key,
    const std::string& secret_key,
    void* send_receiver_handle
) {
    static network::ConnectionManager conn_mgr;
    static network::HttpClient http_client;
    
    RemoteInfo info = parseRemoteInfo(url);
    
    if (https) {
        info.https = true;
        if (info.provider == CloudService::HTTP) {
            info.provider = CloudService::HTTPS;
        }
    }
    
    switch (info.provider) {
        case CloudService::HTTP:
        case CloudService::HTTPS: {
            return std::make_unique<HTTPProvider>(
                info.endpoint, info.port, info.https, conn_mgr, http_client);
        }
        case CloudService::AWS:
        case CloudService::MinIO:
        case CloudService::Azure:
        case CloudService::GCP:
        case CloudService::Oracle:
        case CloudService::IBM: {
            std::cerr << "[ERROR] Provider not yet implemented: " 
                      << static_cast<int>(info.provider) << std::endl;
            std::cerr << "[INFO] This feature will be available in Version 3+" << std::endl;
            return nullptr;
        }
        default: {
            std::cerr << "[ERROR] Unsupported cloud service: " 
                      << static_cast<int>(info.provider) << std::endl;
            return nullptr;
        }
    }
    
    return nullptr;
}

network::HttpResponse Provider::download(const std::string& file_path, 
                                        uint64_t offset, 
                                        uint64_t length) {
    std::cerr << "[DEBUG] Provider::download: file_path=" << file_path << std::endl;
    
    if (!conn_mgr_ || !http_client_) {
        std::cerr << "[ERROR] ConnectionManager or HttpClient not initialized" << std::endl;
        return network::HttpResponse{};
    }
    
    bool use_tls = isHTTPS();
    
    auto connection = conn_mgr_->getConnection(address_, port_, use_tls);
    
    if (!connection) {
        std::cerr << "[ERROR] Failed to get connection from pool" << std::endl;
        return network::HttpResponse{};
    }
    
    network::HttpResponse response = http_client_->sendRequest(
        connection,
        "GET",
        file_path,
        offset,
        length
    );
    
    conn_mgr_->returnConnection(connection);
    
    if (!network::HttpResponse::checkSuccess(response.code)) {
        std::cerr << "[ERROR] Download failed: status=" 
                  << network::HttpResponse::getResponseCodeNumber(response.code) << std::endl;
    }
    
    return response;
}

}  // namespace myblob::cloud

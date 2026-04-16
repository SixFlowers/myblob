#include "cloud/provider.hpp"
#include "cloud/aws.hpp"
#include "cloud/azure.hpp"
#include "cloud/gcp.hpp"
#include "cloud/minio.hpp"
#include "cloud/http_provider.hpp"
#include "network/config.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

namespace myblob::cloud {
using namespace std;
// 定义静态成员
bool Provider::testEnviornment = false;

// 远程文件协议前缀
static const char* remoteFile[] = {
    "https://",
    "http://",
    "s3://",
    "azure://",
    "gs://",
    "oci://",
    "ibm://",
    "minio://"
};
static const size_t remoteFileCount = sizeof(remoteFile) / sizeof(remoteFile[0]);

Provider::Provider(myblob::network::ConnectionManager& conn_mgr,
                   myblob::network::HttpClient& http_client,
                   CloudService type)
    : conn_mgr_(&conn_mgr)
    , http_client_(&http_client)
    , type_(type) {
    std::cerr << "[DEBUG] Provider: Created with ConnectionManager, type=" 
              << static_cast<int>(type_) << std::endl;
}

Provider::Provider(const std::string& addr, uint16_t port,
                   myblob::network::ConnectionManager& conn_mgr,
                   myblob::network::HttpClient& http_client,
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
    return CloudService::HTTPS; // 默认HTTPS
}

std::string Provider::getCloudServiceName(CloudService service) {
    switch (service) {
        case CloudService::HTTPS: return "HTTPS";
        case CloudService::HTTP: return "HTTP";
        case CloudService::AWS: return "AWS";
        case CloudService::Azure: return "Azure";
        case CloudService::GCP: return "GCP";
        case CloudService::Oracle: return "Oracle";
        case CloudService::IBM: return "IBM";
        case CloudService::MinIO: return "MinIO";
        default: return "Unknown";
    }
}

std::string Provider::getCloudServiceName(const std::string& url) {
    return getCloudServiceName(getCloudService(url));
}

std::unique_ptr<Provider> Provider::createProvider(
    const RemoteInfo& info,
    myblob::network::ConnectionManager& conn_mgr,
    myblob::network::HttpClient& http_client
) {
    switch (info.provider) {
        case CloudService::HTTP:
        case CloudService::HTTPS: {
            return std::make_unique<HTTPProvider>(
                info.endpoint, info.port, info.https, conn_mgr, http_client);
        }
        case CloudService::AWS:{
            return std::make_unique<AWS>(info,conn_mgr,http_client);
        }
        case CloudService::MinIO:
        case CloudService::Oracle:
        case CloudService::IBM: {
            return std::make_unique<AWS>(info, conn_mgr, http_client);
        }
        case CloudService::Azure:{
            return std::make_unique<Azure>(info, conn_mgr, http_client);
        }
        case CloudService::GCP: {
            return std::make_unique<GCP>(info, conn_mgr, http_client);
        }
        default: {
            std::cerr << "[ERROR] Unsupported cloud service: " 
                      << static_cast<int>(info.provider) << std::endl;
            return nullptr;
        }
    }
    
    return nullptr;
}

// 旧的makeProvider函数，为了向后兼容
std::unique_ptr<Provider> Provider::makeProvider(
    const std::string& url,
    bool https,
    const std::string& access_key,
    const std::string& secret_key,
    void* send_receiver_handle
) {
    static myblob::network::ConnectionManager conn_mgr;
    static myblob::network::HttpClient http_client;
    
    RemoteInfo info = parseRemoteInfo(url);
    
    if (https) {
        info.https = true;
        if (info.provider == CloudService::HTTP) {
            info.provider = CloudService::HTTPS;
        }
    }
    
    return createProvider(info, conn_mgr, http_client);
}

myblob::network::HttpResponse Provider::download(const std::string& file_path,
                                        uint64_t offset,
                                        uint64_t length) {
    std::cerr << "[DEBUG] Provider::download: file_path=" << file_path << std::endl;

    if (!conn_mgr_ || !http_client_) {
        std::cerr << "[ERROR] ConnectionManager or HttpClient not initialized" << std::endl;
        return myblob::network::HttpResponse{};
    }

    bool use_tls = isHTTPS();

    auto connection = conn_mgr_->getConnection(address_, port_, use_tls);

    if (!connection) {
        std::cerr << "[ERROR] Failed to get connection from pool" << std::endl;
        return myblob::network::HttpResponse{};
    }

    myblob::network::HttpResponse response = http_client_->sendRequest(
        connection,
        "GET",
        file_path,
        offset,
        length
    );

    conn_mgr_->returnConnection(connection);

    if (!myblob::network::HttpResponse::checkSuccess(response.code)) {
        std::cerr << "[ERROR] Download failed: status="
                  << myblob::network::HttpResponse::getResponseCodeNumber(response.code) << std::endl;
    }

    return response;
}

// 获取配置
myblob::network::Config Provider::getConfig(myblob::network::TaskedSendReceiverHandle& sendReceiver) {
    // 默认实现返回空配置
    return myblob::network::Config{};
}

// 静态辅助方法
std::string Provider::getETag(std::string_view header) {
    // 默认实现返回空字符串
    return "";
}

std::string Provider::getUploadId(std::string_view body) {
    // 默认实现返回空字符串
    return "";
}

}  // namespace myblob::cloud

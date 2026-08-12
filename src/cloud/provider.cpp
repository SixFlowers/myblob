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
bool Provider::testEnvironment = false;

Provider::Provider(myblob::network::ConnectionManager& conn_mgr,
                   myblob::network::HttpClient& http_client,
                   CloudService type)
    : conn_mgr_(&conn_mgr)
    , http_client_(&http_client)
    , type_(type) {
    // Provider created
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
    // [DEBUG] Provider created with addr/port, type=... (silenced for production)
}
//判断一个 URL 字符串是"远程云存储地址"还是"本地文件路径"
bool Provider::isRemoteFile(const std::string& url) {
    for (size_t i = 0; i < Provider::remoteFileCount; i++) {
        if (url.find(Provider::remoteFile[i]) == 0) {
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
        case CloudService::MinIO: {
            return std::make_unique<MinIO>(info, conn_mgr, http_client);
        }
        case CloudService::Oracle:
        case CloudService::IBM: {
            // TODO: 实现 OracleProvider 和 IBMProvider
            std::cerr << "[ERROR] Oracle and IBM providers not implemented yet" << std::endl;
            return nullptr;
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
    bool https,//调用者强制设置https覆盖
    const std::string& access_key,
    const std::string& secret_key,
    void* /*send_receiver_handle*/
) {
    // NOTE: function-static — all Providers created via makeProvider share
    // the same connection pool & HTTP client. This is fine for simple usage
    // but for production: use createProvider() with explicit lifetime control.
    static myblob::network::ConnectionManager conn_mgr;
    static myblob::network::HttpClient http_client;
    
    RemoteInfo info = parseRemoteInfo(url);
    
    if (https) {
        info.https = true;
        if (info.provider == CloudService::HTTP) {
            info.provider = CloudService::HTTPS;
        }
    }

    if (!access_key.empty() || !secret_key.empty()) {
        switch (info.provider) {
            case CloudService::AWS:
                return std::make_unique<AWS>(info, access_key, secret_key, conn_mgr, http_client);
            case CloudService::MinIO:
                return std::make_unique<MinIO>(info, access_key, secret_key, conn_mgr, http_client);
            case CloudService::Azure:
                return std::make_unique<Azure>(info, access_key, secret_key, conn_mgr, http_client);
            case CloudService::GCP:
                return std::make_unique<GCP>(info, access_key, secret_key, conn_mgr, http_client);
            default:
                break;
        }
    }

    return createProvider(info, conn_mgr, http_client);
}

myblob::network::HttpResponse Provider::download(const std::string& file_path,
                                        uint64_t offset,
                                        uint64_t length) {
    // Provider::download called

    if (!conn_mgr_ || !http_client_) {//构造函数中设置
        std::cerr << "[ERROR] ConnectionManager or HttpClient not initialized" << std::endl;
        return myblob::network::HttpResponse{};
    }

    bool use_tls = isHTTPS();

    // 使用虚函数获取地址和端口，允许子类动态生成（如AWS的S3地址）
    std::string addr = getAddress();
    uint16_t port = getPort();
    
    if (addr.empty()) {
        std::cerr << "[ERROR] Provider address is empty" << std::endl;
        return myblob::network::HttpResponse{};
    }

    auto connection = conn_mgr_->getConnection(addr, port, use_tls);

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

    conn_mgr_->returnConnection(std::move(connection));

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
// 从HTTP响应头中提取ETag
// ETag格式: ETag: "abc123" 或 ETag: abc123  ETag内容校验码
//每个分片各自独立请求、各自独立返回一个 ETag。不是一次性返回全部。
std::string Provider::getETag(std::string_view header) {
    std::string_view needle = "ETag: \"";
    auto pos = header.find(needle);
    if (pos == std::string_view::npos) {
        needle = "etag: \"";
        pos = header.find(needle);
    }
    if (pos == std::string_view::npos) {
        return "";
    }

    pos += needle.size();
    auto end = header.find('"', pos);
    if (end == std::string_view::npos) {
        return "";
    }

    return std::string(header.substr(pos, end - pos));
}

// 从XML响应体中提取UploadId
// 格式: <UploadId>abc123</UploadId>
std::string Provider::getUploadId(std::string_view body) {
    size_t start = body.find("<UploadId>");
    if (start == std::string_view::npos) {
        return "";
    }
    start += 10; // 跳过 "<UploadId>"
    
    size_t end = body.find("</UploadId>", start);
    if (end == std::string_view::npos) {
        return "";
    }
    
    return std::string(body.substr(start, end - start));
}

}  // namespace myblob::cloud

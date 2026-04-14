#pragma once
#include <cstdint>
#include <string>
#include <utility>

namespace myblob::cloud {

enum class CloudService : uint8_t {
    HTTPS = 0,
    HTTP = 1,
    AWS = 2,
    Azure = 3,
    GCP = 4,
    Oracle = 5,
    IBM = 6,
    MinIO = 7,
    Local = 255
};

struct RemoteInfo {
    CloudService provider = CloudService::HTTPS;
    std::string bucket = "";
    std::string region = "";
    std::string endpoint = "";
    std::string path = "";
    uint16_t port = 80;
    bool https = true;
    bool zonal = false;
};

inline RemoteInfo parseRemoteInfo(const std::string& url) {
    RemoteInfo info;
    
    size_t prefix_len = 0;
    if (url.find("https://") == 0) {
        info.https = true;
        info.provider = CloudService::HTTPS;
        info.port = 443;
        prefix_len = 8;
    }
    else if (url.find("http://") == 0) {
        info.https = false;
        info.provider = CloudService::HTTP;
        info.port = 80;
        prefix_len = 7;
    }
    else if (url.find("s3://") == 0) {
        info.provider = CloudService::AWS;
        info.https = true;
        info.port = 443;
        prefix_len = 5;
        
        // 解析 s3://bucket-name/path 格式
        std::string remaining = url.substr(prefix_len);
        size_t slash_pos = remaining.find('/');
        if (slash_pos != std::string::npos) {
            info.bucket = remaining.substr(0, slash_pos);
            info.path = remaining.substr(slash_pos + 1);
        } else {
            info.bucket = remaining;
        }
        // 默认使用 AWS S3 标准端点
        info.endpoint = info.bucket + ".s3.amazonaws.com";
        info.region = "us-east-1";  // 默认区域
        return info;
    }
    else if (url.find("azure://") == 0) {
        info.provider = CloudService::Azure;
        info.port = 443;
        prefix_len = 9;
    }
    else if (url.find("gs://") == 0) {
        info.provider = CloudService::GCP;
        info.port = 443;
        prefix_len = 5;
    }
    else if (url.find("minio://") == 0) {
        info.provider = CloudService::MinIO;
        info.port = 9000;
        prefix_len = 9;
    }
    else if (url.find("oci://") == 0) {
        info.provider = CloudService::Oracle;
        info.port = 443;
        prefix_len = 6;
    }
    else if (url.find("ibm://") == 0) {
        info.provider = CloudService::IBM;
        info.port = 443;
        prefix_len = 6;
    }
    
    if (prefix_len > 0) {
        std::string remaining = url.substr(prefix_len);
        size_t slash_pos = remaining.find('/');
        size_t colon_pos = remaining.find(':');
        
        std::string host_port;
        if (slash_pos != std::string::npos && colon_pos != std::string::npos) {
            host_port = remaining.substr(0, std::min(slash_pos, colon_pos));
        } else if (slash_pos != std::string::npos) {
            host_port = remaining.substr(0, slash_pos);
        } else if (colon_pos != std::string::npos) {
            host_port = remaining.substr(0, colon_pos);
        } else {
            host_port = remaining;
        }
        
        size_t colon = host_port.find(':');
        if (colon != std::string::npos) {
            info.endpoint = host_port.substr(0, colon);
            std::string port_str = host_port.substr(colon + 1);
            info.port = static_cast<uint16_t>(std::stoi(port_str));
        } else {
            info.endpoint = host_port;
        }
        
        // 提取路径
        if (slash_pos != std::string::npos) {
            info.path = remaining.substr(slash_pos + 1);
        }
    }
    
    return info;
}

}  // namespace myblob::cloud

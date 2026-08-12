#pragma once
#include <cstdint>
#include <string>
#include <utility>

namespace myblob::cloud
{

    enum class CloudService : uint8_t
    {
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

    struct RemoteInfo
    {
        CloudService provider = CloudService::HTTPS;
        std::string bucket = "";
        std::string region = "";
        std::string endpoint = ""; // ip或者域名,不包含端口
        std::string path = "";
        uint16_t port = 80;
        bool https = true;
        bool zonal = false;
    };

    inline RemoteInfo parseRemoteInfo(const std::string &url)
    {
        RemoteInfo info;

        size_t prefix_len = 0;
        if (url.find("https://") == 0)
        {
            info.https = true;
            info.provider = CloudService::HTTPS;
            info.port = 443;
            prefix_len = 8;
        }
        else if (url.find("http://") == 0)
        {
            info.https = false;
            info.provider = CloudService::HTTP;
            info.port = 80;
            prefix_len = 7;
        }
        else if (url.find("s3://") == 0)
        {
            info.provider = CloudService::AWS;
            info.https = true;
            info.port = 443;
            prefix_len = 5;
            info.https = true;

            // 解析 s3://bucket-name/path 格式
            std::string remaining = url.substr(prefix_len);
            size_t slash_pos = remaining.find('/');
            if (slash_pos != std::string::npos)
            {
                info.bucket = remaining.substr(0, slash_pos);
                info.path = remaining.substr(slash_pos + 1);
            }
            else
            {
                info.bucket = remaining;
            }
            // 默认使用 AWS S3 标准端点
            info.endpoint = info.bucket + ".s3.amazonaws.com";
            info.region = "us-east-1"; // 默认区域
            return info;
        }
        else if (url.find("azure://") == 0)
        {
            info.provider = CloudService::Azure;
            info.https = true;
            info.port = 443;
            prefix_len = 8;
        }
        else if (url.find("gs://") == 0)
        {
            info.provider = CloudService::GCP;
            info.https = true;
            info.port = 443;
            prefix_len = 5;
        }
        else if (url.find("minio://") == 0)
        {
            // minio://host:port/bucket/path
            // 端口决定协议: 443→HTTPS, 其它→HTTP (默认端口 9000)
            info.provider = CloudService::MinIO;
            prefix_len = 8;

            std::string remaining = url.substr(prefix_len);
            size_t slash_pos = remaining.find('/');
            std::string host_port = slash_pos != std::string::npos
                                        ? remaining.substr(0, slash_pos)
                                        : remaining;

            size_t colon = host_port.find(':');
            if (colon != std::string::npos)
            {
                info.endpoint = host_port.substr(0, colon);
                info.port = static_cast<uint16_t>(std::stoi(host_port.substr(colon + 1)));
            }
            else
            {
                info.endpoint = host_port;
                info.port = 9000;
            }
            // 端口 443 自动走 HTTPS，其他端口默认 HTTP
            info.https = (info.port == 443);

            if (slash_pos != std::string::npos)
            {
                auto bucket_and_path = remaining.substr(slash_pos + 1);
                size_t bucket_end = bucket_and_path.find('/');
                if (bucket_end != std::string::npos)
                {
                    info.bucket = bucket_and_path.substr(0, bucket_end);
                    info.path = bucket_and_path.substr(bucket_end + 1);
                }
                else
                {
                    info.bucket = bucket_and_path;
                }
            }
            return info;
        }
        else if (url.find("oci://") == 0)
        {
            info.provider = CloudService::Oracle;
            info.https = true;
            info.port = 443;
            prefix_len = 6;
        }
        else if (url.find("ibm://") == 0)
        {
            info.provider = CloudService::IBM;
            info.https = true;
            info.port = 443;
            prefix_len = 6;
        }
        // 通用Url处理逻辑
        if (prefix_len > 0)
        {
            std::string remaining = url.substr(prefix_len);
            size_t slash_pos = remaining.find('/');

            // host:port 部分 = 第一个 '/' 之前的所有内容，如果没有 '/' 就是整个 remaining
            std::string host_port;
            if (slash_pos != std::string::npos)
            {
                host_port = remaining.substr(0, slash_pos);
            }
            else
            {
                host_port = remaining;
            }

            // 从 host_port 中分离 host 和 port（用最后一个 ':' 支持 IPv6）
            size_t colon = host_port.rfind(':');
            if (colon != std::string::npos)
            {
                info.endpoint = host_port.substr(0, colon);
                std::string port_str = host_port.substr(colon + 1);
                info.port = static_cast<uint16_t>(std::stoi(port_str));
            }
            else
            {
                info.endpoint = host_port;
            }

            // 提取路径
            if (slash_pos != std::string::npos)
            {
                info.path = remaining.substr(slash_pos + 1);
            }
        }

        return info;
    }

} // namespace myblob::cloud

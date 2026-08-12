#pragma once
#include "../network/http_request.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace myblob::cloud {
//AWS签名算法的纯计算工具类
class AWSSigner {
public:
    struct StringToSign {
        myblob::network::HttpRequest& request;//待签名的 HTTP 请求引用
        std::string region;//WS 区域，如 us-east-1
        std::string service;//服务名，固定 "s3"
        std::string requestSHA;//规范请求的 SHA256 哈希
        std::string signedHeaders;//参与签名的头部列表（分号分隔小写key）
        std::string payloadHash;//请求体的 SHA256 哈希
    };

    // 编码规范请求，构建规范请求（SigV4 Task 1），计算 requestSHA、signedHeaders、payloadHash。
    static void encodeCanonicalRequest(myblob::network::HttpRequest& request,
                                       StringToSign& stringToSign,
                                       const uint8_t* bodyData = nullptr,
                                       uint64_t bodyLength = 0);

    // 创建签名请求：返回 path+query，并将 Authorization 头加入 request.headers
    [[nodiscard]] static std::string createSignedRequest(const std::string& keyId,
                                           const std::string& secret,
                                           const StringToSign& stringToSign);

private:
    // 构建待签名字符串
    [[nodiscard]] static std::string createStringToSign(const StringToSign& stringToSign);
};

}

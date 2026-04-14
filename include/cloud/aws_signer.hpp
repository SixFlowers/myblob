#pragma once
#include "../network/http_request.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace myblob::cloud {

class AWSSigner {
public:
    struct StringToSign {
        myblob::network::HttpRequest& request;
        std::string region;
        std::string service;
        std::string requestSHA;
        std::string signedHeaders;
        std::string payloadHash;
    };

    // 编码规范请求
    static void encodeCanonicalRequest(myblob::network::HttpRequest& request,
                                       StringToSign& stringToSign,
                                       const uint8_t* bodyData = nullptr,
                                       uint64_t bodyLength = 0);

    // 创建签名请求
    static std::string createSignedRequest(const std::string& keyId,
                                           const std::string& secret,
                                           const StringToSign& stringToSign);
};

}

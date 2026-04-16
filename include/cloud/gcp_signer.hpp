#pragma once
#include "network/http_request.hpp"
#include <string>

namespace myblob::cloud {
  class GCPSigner {
public:
    /// 待签名字符串结构
    struct StringToSign {
        std::string region;       // 区域
        std::string service;      // 服务（storage）
        std::string signedHeaders; // 已签名头部
    };

    /// 创建签名请求
    /// @param serviceAccountEmail 服务账户邮箱
    /// @param privateRSA 私钥（RSA 格式）
    /// @param request HTTP 请求
    /// @param stringToSign 待签名字符串信息
    /// @return 签名后的 Authorization 头部值
    [[nodiscard]] static std::string createSignedRequest(
        const std::string& serviceAccountEmail, 
        const std::string& privateRSA, 
        network::HttpRequest& request, 
        StringToSign& stringToSign);
};

} // namespace myblob::cloud
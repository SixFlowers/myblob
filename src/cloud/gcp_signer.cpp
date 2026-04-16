#include "cloud/gcp_signer.hpp"
#include "utils/utils.hpp"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <iomanip>
#include <sstream>

namespace myblob::cloud {

using namespace std;

// ============================================================================
// GCPSigner::createSignedRequest - 创建签名请求
// ============================================================================
string GCPSigner::createSignedRequest(
    const string& serviceAccountEmail, 
    const string& privateRSA, 
    network::HttpRequest& request, 
    StringToSign& stringToSign)
{
    // 简化版：使用 HMAC-SHA256 签名（与 AWS 兼容模式）
    // 完整实现应支持 RSA-SHA256
    
    // 构建待签名字符串
    stringstream ss;
    ss << network::HttpRequest::getRequestMethod(request.method) << "\n";
    ss << "\n";  // Content-MD5
    ss << "\n";  // Content-Type
    ss << "\n";  // Date
    
    // 规范化头部
    ss << "host:" << request.headers["Host"] << "\n";
    ss << "x-amz-date:" << request.headers["x-amz-date"] << "\n";
    
    // 规范化资源
    ss << request.path;
    
    // 添加查询参数
    if (!request.queries.empty()) {
        ss << "?";
        bool first = true;
        for (const auto& q : request.queries) {
            if (!first) ss << "&";
            ss << q.first;
            if (!q.second.empty()) {
                ss << "=" << q.second;
            }
            first = false;
        }
    }
    
    string stringToSignStr = ss.str();
    
    // 解码 Base64 私钥（假设是 HMAC 密钥）
    auto decodedKey = utils::base64Decode(
        reinterpret_cast<const uint8_t*>(privateRSA.data()), 
        privateRSA.size());
    
    // HMAC-SHA256 签名
    unsigned int sigLen = 0;
    uint8_t signature[EVP_MAX_MD_SIZE];
    
    HMAC(EVP_sha256(),
         decodedKey.first.get(), static_cast<int>(decodedKey.second),
         reinterpret_cast<const uint8_t*>(stringToSignStr.data()), stringToSignStr.size(),
         signature, &sigLen);
    
    // Base64 编码
    string sigBase64 = utils::base64Encode(signature, sigLen);
    
    // 构建 Authorization 头部
    stringstream auth;
    auth << "AWS " << serviceAccountEmail << ":" << sigBase64;
    
    return auth.str();
}

} // namespace myblob::cloud
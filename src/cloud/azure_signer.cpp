#include "cloud/azure_signer.hpp"
#include "utils/utils.hpp"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace myblob::cloud {

using namespace std;

string AzureSigner::createSignedRequest(
    const string& serviceAccountEmail,
    const string& privateRSA,
    network::HttpRequest& request)
{
    stringstream stringToSign;

    // 1. HTTP verb
    stringToSign << network::HttpRequest::getRequestMethod(request.method) << "\n";

    // 2. Content-Encoding
    stringToSign << "\n";

    // 3. Content-Language
    stringToSign << "\n";

    // 4. Content-Length
    auto it = request.headers.find("Content-Length");
    if (it != request.headers.end()) {
        stringToSign << it->second;
    }
    stringToSign << "\n";

    // 5. Content-MD5
    stringToSign << "\n";

    // 6. Content-Type
    stringToSign << "\n";

    // 7. Date (使用 x-ms-date 代替)
    stringToSign << "\n";

    // 8. If-Modified-Since
    stringToSign << "\n";

    // 9. If-Match
    stringToSign << "\n";

    // 10. If-None-Match
    stringToSign << "\n";

    // 11. If-Unmodified-Since
    stringToSign << "\n";

    // 12. Range
    stringToSign << "\n";

    // 13. CanonicalizedHeaders (x-ms- 头部，按字母顺序)
    vector<pair<string, string>> msHeaders;
    for (const auto& h : request.headers) {
        if (h.first.find("x-ms-") == 0) {
            msHeaders.emplace_back(h.first, h.second);
        }
    }
    sort(msHeaders.begin(), msHeaders.end());
    for (const auto& h : msHeaders) {
        stringToSign << h.first << ":" << h.second << "\n";
    }

    // 14. CanonicalizedResource
    stringToSign << "/" << serviceAccountEmail << request.path;

    // 构建待签名字符串
    string stringToSignStr = stringToSign.str();

    // 解码 Base64 私钥
    auto decodedKey = utils::base64Decode(
        reinterpret_cast<const uint8_t*>(privateRSA.data()),
        privateRSA.size());

    // HMAC-SHA256 签名
    unsigned int sigLen = 0;
    uint8_t signature[EVP_MAX_MD_SIZE];

    HMAC(EVP_sha256(),
         decodedKey.first.get(),
         static_cast<int>(decodedKey.second),
         reinterpret_cast<const uint8_t*>(stringToSignStr.data()),
         stringToSignStr.size(),
         signature,
         &sigLen);

    // Base64 编码签名结果
    string sigBase64 = utils::base64Encode(signature, sigLen);

    // 构建 Authorization 头部
    stringstream auth;
    auth << "SharedKey " << serviceAccountEmail << ":" << sigBase64;

    return auth.str();
}

}
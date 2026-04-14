#include "cloud/aws_signer.hpp"
#include "utils/utils.hpp"
#include <algorithm>
#include <iomanip>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <sstream>

namespace myblob::cloud {

using namespace std;

// 辅助函数：计算SHA256
static string sha256(const uint8_t* data, uint64_t length) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return "";
    
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }
    
    if (EVP_DigestUpdate(ctx, data, length) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }
    
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    if (EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }
    
    EVP_MD_CTX_free(ctx);
    
    stringstream ss;
    for (unsigned int i = 0; i < hash_len; i++) {
        ss << hex << setw(2) << setfill('0') << (int)hash[i];
    }
    return ss.str();
}

// 辅助函数：计算HMAC-SHA256
static string hmac_sha256(const string& key, const string& data) {
    unsigned char result[EVP_MAX_MD_SIZE];
    unsigned int result_len = 0;
    
    HMAC(EVP_sha256(), key.c_str(), key.length(),
         reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
         result, &result_len);
    
    return string(reinterpret_cast<char*>(result), result_len);
}

// 辅助函数：计算HMAC-SHA256（二进制密钥）
static string hmac_sha256_binary(const string& key, const string& data) {
    unsigned char result[EVP_MAX_MD_SIZE];
    unsigned int result_len = 0;
    
    HMAC(EVP_sha256(), key.data(), key.length(),
         reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
         result, &result_len);
    
    return string(reinterpret_cast<char*>(result), result_len);
}

// 辅助函数：十六进制编码
static string hex_encode(const string& data) {
    stringstream ss;
    for (unsigned char c : data) {
        ss << hex << setw(2) << setfill('0') << (int)c;
    }
    return ss.str();
}

// 辅助函数：URI编码
static string uri_encode(const string& value) {
    ostringstream encoded;
    encoded.fill('0');
    encoded << hex;
    
    for (char c : value) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else {
            encoded << '%' << setw(2) << uppercase << (int)(unsigned char)c;
        }
    }
    
    return encoded.str();
}

void AWSSigner::encodeCanonicalRequest(network::HttpRequest& request,
                                       StringToSign& stringToSign,
                                       const uint8_t* bodyData,
                                       uint64_t bodyLength)
{
    stringstream canonicalRequest;
    
    // 1. HTTP方法
    canonicalRequest << network::HttpRequest::getRequestMethod(request.method) << "\n";
    
    // 2. URI路径
    if(request.path.empty()){
        canonicalRequest << "/\n";
    }else{
        canonicalRequest << request.path << "\n";
    }
    
    // 3. 查询字符串（按字母顺序排序）
    if (!request.queries.empty()) {
        vector<pair<string, string>> sortedQueries(request.queries.begin(), request.queries.end());
        sort(sortedQueries.begin(), sortedQueries.end());
        
        bool first = true;
        for (const auto& [key, value] : sortedQueries) {
            if (!first) canonicalRequest << "&";
            canonicalRequest << uri_encode(key);
            if (!value.empty()) {
                canonicalRequest << "=" << uri_encode(value);
            }
            first = false;
        }
    }
    canonicalRequest << "\n";
    
    // 4. 请求头（按字母顺序排序）
    vector<pair<string, string>> sortedHeaders(request.headers.begin(), request.headers.end());
    sort(sortedHeaders.begin(), sortedHeaders.end());
    
    string signedHeaders;
    for (const auto& [key, value] : sortedHeaders) {
        string lowerKey = key;
        transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), ::tolower);
        canonicalRequest << lowerKey << ":" << value << "\n";
        if (!signedHeaders.empty()) signedHeaders += ";";
        signedHeaders += lowerKey;
    }
    canonicalRequest << "\n";
    
    // 5. 签名的头部列表
    canonicalRequest << signedHeaders << "\n";
    
    // 6. 请求体哈希
    string payloadHash;
    if (bodyData && bodyLength > 0) {
        payloadHash = sha256(bodyData, bodyLength);
    } else {
        payloadHash = sha256(reinterpret_cast<const uint8_t*>(""), 0);
    }
    canonicalRequest << payloadHash;
    
    // 存储结果
    stringToSign.requestSHA = sha256(
        reinterpret_cast<const uint8_t*>(canonicalRequest.str().data()),
        canonicalRequest.str().length()
    );
    stringToSign.signedHeaders = signedHeaders;
    stringToSign.payloadHash = payloadHash;
}

string AWSSigner::createSignedRequest(const string& keyId,
                                      const string& secret,
                                      const StringToSign& stringToSign)
{
    // 1. 构建待签名字符串
    stringstream stringToSignStream;
    stringToSignStream << "AWS4-HMAC-SHA256\n";
    
    // 获取时间戳
    string timestamp;
    auto it = stringToSign.request.headers.find("x-amz-date");
    if (it != stringToSign.request.headers.end()) {
        timestamp = it->second;
    } else {
        timestamp = "20240101T000000Z"; // 默认时间戳
    }
    stringToSignStream << timestamp << "\n";
    
    // 构建凭证范围
    string date = timestamp.substr(0, 8);
    string credentialScope = date + "/" + stringToSign.region + "/" + stringToSign.service + "/aws4_request";
    stringToSignStream << credentialScope << "\n";
    stringToSignStream << stringToSign.requestSHA;
    
    // 2. 计算签名密钥
    string kSecret = "AWS4" + secret;
    string kDate = hmac_sha256_binary(kSecret, date);
    string kRegion = hmac_sha256_binary(kDate, stringToSign.region);
    string kService = hmac_sha256_binary(kRegion, stringToSign.service);
    string kSigning = hmac_sha256_binary(kService, "aws4_request");
    
    // 3. 计算最终签名
    string signature = hmac_sha256_binary(kSigning, stringToSignStream.str());
    string signatureHex = hex_encode(signature);
    
    // 4. 构建授权头
    stringstream authHeader;
    authHeader << "AWS4-HMAC-SHA256 Credential=" << keyId << "/" << credentialScope
               << ", SignedHeaders=" << stringToSign.signedHeaders
               << ", Signature=" << signatureHex;
    
    return authHeader.str();
}

}

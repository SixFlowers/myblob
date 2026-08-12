#include "cloud/aws_signer.hpp"
#include "utils/utils.hpp"
#include <algorithm>
#include <map>
#include <sstream>
#include <stdexcept>
#include <openssl/md5.h>

namespace myblob::cloud {

using namespace std;

void AWSSigner::encodeCanonicalRequest(network::HttpRequest& request,
                                       StringToSign& stringToSign,
                                       const uint8_t* bodyData,
                                       uint64_t bodyLength)
{
    /*PUT 请求的规范请求格式.假设上传一个 100 字节的对象，多部分上传第 1 片：
    PUT
    /test-bucket/myfile.txt
    partNumber=1&uploadId=abcdef123456
 
    content-length:100
    content-md5:XrY7u+Ae7tCTyyK7j1rNww==
    host:test-bucket.s3.us-east-1.amazonaws.com
    x-amz-content-sha256:a1b2c3d4...（100字节的SHA256）
    x-amz-date:20260426T120000Z
 
    content-length;content-md5;host;x-amz-content-sha256;x-amz-date
    a1b2c3d4...（100字节的SHA256）
    */
    stringstream requestStream;

    // Step 1: HTTP方法
    requestStream << network::HttpRequest::getRequestMethod(request.method) << "\n";

    // Step 2: URI路径
    if (request.path.empty())
        requestStream << "/\n";
    else
        requestStream << request.path << "\n";

    // Step 3: 查询字符串（std::map已按key排序，URL编码，空value也要带=）
    if (request.queries.size()) {
        auto it = request.queries.begin();
        while (it != request.queries.end()) {
            requestStream << utils::encodeUrlParameters(it->first) << "=" << utils::encodeUrlParameters(it->second);
            if (++it != request.queries.end())
                requestStream << "&";
        }
    }
    requestStream << "\n";

    // Step 6 (提前): 计算payload哈希并加入请求头；计算请求体 SHA256（≤1KB 时计算哈希，>1KB 用 UNSIGNED-PAYLOAD），写入 x-amz-content-sha256 头；PUT/POST 还加 Content-MD5
    if (bodyLength <= (1 << 10)) {
        stringToSign.payloadHash = utils::sha256Encode(bodyData, bodyLength);
        request.headers.emplace("x-amz-content-sha256", stringToSign.payloadHash);

        // PUT/POST 请求添加 Content-MD5
        if (request.method == network::HttpRequest::Method::PUT || request.method == network::HttpRequest::Method::POST) {
            auto md5 = utils::md5Encode(bodyData, bodyLength);
            auto encodedResult = utils::base64Encode(reinterpret_cast<const uint8_t*>(md5.data()), MD5_DIGEST_LENGTH);
            request.headers.emplace("Content-MD5", encodedResult);
        }
    } else {
        stringToSign.payloadHash = "UNSIGNED-PAYLOAD";
        request.headers.emplace("x-amz-content-sha256", stringToSign.payloadHash);
    }

    // Step 4: 请求头（按小写key排序）
    map<string, string> sorted;
    if (request.headers.size()) {
        auto it = request.headers.begin();
        while (it != request.headers.end()) {
            string val = it->first;
            transform(val.begin(), val.end(), val.begin(), [](unsigned char c) { return tolower(c); });
            sorted.emplace(val, it->second);
            ++it;
        }
        for (const auto& h : sorted)
            requestStream << h.first << ":" << h.second << "\n";
    }
    requestStream << "\n";

    // Step 5: 签名头列表
    if (sorted.size()) {
        stringstream signedRequests;
        auto it = sorted.begin();
        while (it != sorted.end()) {
            signedRequests << it->first;
            if (++it != sorted.end())
                signedRequests << ";";
        }
        stringToSign.signedHeaders = signedRequests.str();
        requestStream << stringToSign.signedHeaders;
    }
    requestStream << "\n";

    // Step 6 继续: payload哈希
    requestStream << stringToSign.payloadHash;

    // Step 7: 对整个规范请求计算SHA256
    auto requestString = requestStream.str();
    stringToSign.requestSHA = utils::sha256Encode(
    reinterpret_cast<const uint8_t*>(requestString.data()), requestString.length());
}
//构建待签名字符串
string AWSSigner::createStringToSign(const StringToSign& stringToSign)
{
    auto it = stringToSign.request.headers.find("x-amz-date");
    if (it == stringToSign.request.headers.end())
        throw runtime_error("missing x-amz-date");

    stringstream requestStream;
    requestStream << "AWS4-HMAC-SHA256\n";
    requestStream << it->second << "\n";
    requestStream << it->second.substr(0, 8)/*时间格式固定*/ << "/" << stringToSign.region << "/" << stringToSign.service << "/aws4_request\n";
    requestStream << stringToSign.requestSHA;
    return requestStream.str();
}
//计算签名并构建最终请求
string AWSSigner::createSignedRequest(const string& keyId,//类似于账户
                                      const string& secret,//类似于密码
                                      const StringToSign& stringToSign)//待签名的请求信息
{
    // Step 1: 构建派生签名密钥
    auto it = stringToSign.request.headers.find("x-amz-date");
    if (it == stringToSign.request.headers.end())
        throw runtime_error("missing x-amz-date");

    string kRequest = "aws4_request";
    auto kSecret = "AWS4" + secret;
    auto date = it->second.substr(0, 8);
    //密钥+消息生成指纹,先根据kSecret和data生成32位指纹(基于哈希的消息认证码)，然后再通过上一轮生成的指纹作为密钥生成下一轮指纹以此类推
    auto derivedSigningKey = utils::hmacSign(
        reinterpret_cast<const uint8_t*>(kSecret.data()), kSecret.length(),
        reinterpret_cast<const uint8_t*>(date.data()), date.length());
    derivedSigningKey = utils::hmacSign(
        derivedSigningKey.first.get(), derivedSigningKey.second,
        reinterpret_cast<const uint8_t*>(stringToSign.region.data()), stringToSign.region.length());
    derivedSigningKey = utils::hmacSign(
        derivedSigningKey.first.get(), derivedSigningKey.second,
        reinterpret_cast<const uint8_t*>(stringToSign.service.data()), stringToSign.service.length());
    derivedSigningKey = utils::hmacSign(
        derivedSigningKey.first.get(), derivedSigningKey.second,
        reinterpret_cast<const uint8_t*>(kRequest.data()), kRequest.length());

    // Step 2: 用派生密钥签名 stringToSign(先调用 createStringToSign() 生成待签名字符串，再用派生密钥对其做 HMAC。)
    auto stringToSignString = createStringToSign(stringToSign);
    derivedSigningKey = utils::hmacSign(
        derivedSigningKey.first.get(), derivedSigningKey.second,
        reinterpret_cast<const uint8_t*>(stringToSignString.data()), stringToSignString.length());
    const auto signature = utils::hexEncode(derivedSigningKey.first.get(), derivedSigningKey.second);

    // Step 3: 构建 Authorization 头并加入 request.headers
    stringstream authorization;
    authorization << "AWS4-HMAC-SHA256"
                  << " Credential=" << keyId << "/" << date << "/" << stringToSign.region << "/" << stringToSign.service << "/" << kRequest
                  << ", SignedHeaders=" << stringToSign.signedHeaders
                  << ", Signature=" << signature;

    stringToSign.request.headers.emplace("Authorization", authorization.str());

    // Step 4: 构建并返回 path + "?" + queryString（用于HTTP请求行）
    stringstream queryStream;
    if (stringToSign.request.queries.size()) {
        auto qit = stringToSign.request.queries.begin();
        while (qit != stringToSign.request.queries.end()) {
            //签名时查询参数需要 URL 编码
            queryStream << utils::encodeUrlParameters(qit->first) << "=" << utils::encodeUrlParameters(qit->second);
            if (++qit != stringToSign.request.queries.end())
                queryStream << "&";
        }
    }
    return (stringToSign.request.path.empty() ? "/" : stringToSign.request.path) + "?" + queryStream.str();
}

}

#include "cloud/aws.hpp"
#include "network/http_helper.hpp"
#include "network/original_message.hpp"
#include "network/tasked_send_receiver.hpp"
#include "utils/data_vector.hpp"
#include "utils/utils.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>

namespace myblob::cloud {

using namespace std;

// 线程本地存储定义
thread_local shared_ptr<AWS::Secret> AWS::_secret = nullptr;
thread_local shared_ptr<AWS::Secret> AWS::_sessionSecret = nullptr;
thread_local AWS* AWS::_validInstance = nullptr;

// 服务函数：构造AWS时间戳
static string buildAMZTimestamp() {
    stringstream s;
    const auto t = chrono::system_clock::to_time_t(chrono::system_clock::now());
    s << put_time(gmtime(&t), "%Y%m%dT%H%M%SZ");
    return s.str();
}

// 辅助函数：转换IAM时间戳
static int64_t convertIAMTimestamp(string awsTimestamp) {
    istringstream s(awsTimestamp);
    tm t{};
    s >> get_time(&t, "%Y-%m-%dT%H:%M:%SZ");
    return mktime(&t);
}

unique_ptr<utils::DataVector<uint8_t>> AWS::buildRequest(
    network::HttpRequest& request,
    const uint8_t* bodyData,
    uint64_t bodyLength,
    bool initHeaders) const
{
    shared_ptr<Secret> secret;
    if (initHeaders) {
        // 添加必须头部
        request.headers.emplace("Host", getAddress());
        request.headers.emplace("x-amz-date", testEnviornment ? fakeAMZTimestamp : buildAMZTimestamp());
        if (!_settings.zonal) {
            // 添加请求付款人头部
            request.headers.emplace("x-amz-request-payer", "requester");
            secret = _secret;
            // 添加会话令牌
            if (!secret->token.empty()) {
                request.headers.emplace("x-amz-security-token", secret->token);
            }
        } else {
            // 区域端点：使用会话凭证
            secret = _sessionSecret;
            request.headers.emplace("x-amz-s3session-token", secret->token);
        }
    }
    // 创建签名
    AWSSigner::StringToSign stringToSign = {
        .request = request,
        .region = _settings.region,
        .service = "s3",
        .requestSHA = "",
        .signedHeaders = "",
        .payloadHash = ""
    };
    // 创建规范请求
    AWSSigner::encodeCanonicalRequest(request, stringToSign, bodyData, bodyLength);
    // 创建签名请求
    string httpHeader = network::HttpRequest::getRequestMethod(request.method);
    httpHeader += " ";
    httpHeader += AWSSigner::createSignedRequest(secret->keyId, secret->secret, stringToSign);
    httpHeader += " ";
    httpHeader += network::HttpRequest::getRequestType(request.type);
    httpHeader += "\r\n";
    // 添加所有头部
    for (const auto& h : request.headers) {
        httpHeader += h.first + ": " + h.second + "\r\n";
    }
    httpHeader += "\r\n";
    return make_unique<utils::DataVector<uint8_t>>(
        reinterpret_cast<uint8_t*>(httpHeader.data()),
        reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size())
    );
}

unique_ptr<utils::DataVector<uint8_t>> AWS::getRequest(
    const string& filePath,
    const pair<uint64_t, uint64_t>& range) const
{
    // 检查凭证有效性
    if (!validKeys() || (_settings.zonal && !validSession())) {
        return nullptr;
    }
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::GET;
    request.type = network::HttpRequest::Type::HTTP_1_1;
    // 构建请求路径
    if (_settings.endpoint.empty()) {
        request.path = "/" + filePath;
    } else {
        request.path = "/" + _settings.bucket + "/" + filePath;
    }
    // 添加Range头部
    if (range.first != range.second) {
        stringstream rangeString;
        rangeString << "bytes=" << range.first << "-" << range.second;
        request.headers.emplace("Range", rangeString.str());
    }
    return buildRequest(request);
}

unique_ptr<utils::DataVector<uint8_t>> AWS::putRequestGeneric(
    const string& filePath,
    string_view object,
    uint16_t part,
    string_view uploadId) const
{
    if (!validKeys() || (_settings.zonal && !validSession())) {
        return nullptr;
    }
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::PUT;
    request.type = network::HttpRequest::Type::HTTP_1_1;
    auto bodyData = reinterpret_cast<const uint8_t*>(object.data());
    // 构建请求路径
    if (_settings.endpoint.empty()) {
        request.path = "/" + filePath;
    } else {
        request.path = "/" + _settings.bucket + "/" + filePath;
    }
    // 多部分上传参数
    if (part) {
        request.queries.emplace("partNumber", to_string(part));
        request.queries.emplace("uploadId", uploadId);
    }
    // 添加Content-Length头部
    auto bodyLength = object.size();
    request.headers.emplace("Content-Length", to_string(bodyLength));
    return buildRequest(request, bodyData, bodyLength);
}

unique_ptr<utils::DataVector<uint8_t>> AWS::deleteRequestGeneric(
    const string& filePath,
    string_view uploadId) const
{
    if (!validKeys() || (_settings.zonal && !validSession())) {
        return nullptr;
    }
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::DELETE;
    request.type = network::HttpRequest::Type::HTTP_1_1;
    if (_settings.endpoint.empty()) {
        request.path = "/" + filePath;
    } else {
        request.path = "/" + _settings.bucket + "/" + filePath;
    }
    if (!uploadId.empty()) {
        request.queries.emplace("uploadId", uploadId);
    }
    return buildRequest(request);
}

unique_ptr<utils::DataVector<uint8_t>> AWS::createMultiPartRequest(
    const string& filePath) const
{
    // 1. 验证凭证有效
    if (!validKeys() || (_settings.zonal && !validSession()))
        return nullptr;

    // 2. 创建HTTP POST请求
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::POST;
    request.type = network::HttpRequest::Type::HTTP_1_1;

    // 3. 构建请求路径
    if (_settings.endpoint.empty())
        request.path = "/" + filePath;
    else
        request.path = "/" + _settings.bucket + "/" + filePath;

    // 4. 添加uploads查询参数，触发多部分上传初始化
    request.queries.emplace("uploads", "");

    // 5. 构建并签名请求
    return buildRequest(request);
}

unique_ptr<utils::DataVector<uint8_t>> AWS::completeMultiPartRequest(
    const string& filePath,
    string_view uploadId,
    const vector<string>& etags,
    string& content) const
{
    // 1. 验证凭证有效
    if (!validKeys() || (_settings.zonal && !validSession()))
        return nullptr;

    content = "<CompleteMultipartUpload>\n";
    for (auto i = 0ull; i < etags.size(); i++) {
        content += "<Part>\n<PartNumber>";
        content += to_string(i + 1);
        content += "</PartNumber>\n<ETag>\"";
        content += etags[i];
        content += "\"</ETag>\n</Part>\n";
    }
    content += "</CompleteMultipartUpload>\n";

    // 构建请求
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::POST;
    request.type = network::HttpRequest::Type::HTTP_1_1;

    // 构建请求路径
    if (_settings.endpoint.empty())
        request.path = "/" + filePath;
    else
        request.path = "/" + _settings.bucket + "/" + filePath;

    // 添加uploadId查询参数
    request.queries.emplace("uploadId", uploadId);

    // 添加Content-Length头部
    auto bodyData = reinterpret_cast<const uint8_t*>(content.data());
    auto bodyLength = content.size();
    request.headers.emplace("Content-Length", to_string(bodyLength));

    return buildRequest(request, bodyData, bodyLength);
}

string AWS::getAddress() const
{
    // 自定义端点优先
    if (!_settings.endpoint.empty()) {
        return _settings.endpoint;
    }
    // 区域端点
    if (_settings.zonal) {
        auto bucket = _settings.bucket.substr(0, _settings.bucket.size() - 6);
        bucket = bucket.substr(bucket.size() - 11);
        auto find = bucket.find("--");
        auto zone = bucket.substr(find + 2);
        return _settings.bucket + ".s3express-" + zone + "." + _settings.region + ".amazonaws.com";
    }
    return _settings.bucket + ".s3." + _settings.region + ".amazonaws.com";
}

uint16_t AWS::getPort() const
{
    return _settings.port;
}

bool AWS::validKeys(uint32_t offset) const
{
    if (!_secret || _validInstance != this || ((!_secret->token.empty() && _secret->expiration - offset < chrono::system_clock::to_time_t(chrono::system_clock::now())) || _secret->secret.empty()))
        return false;
    return true;
}

bool AWS::validSession(uint32_t offset) const
{
    if (!_sessionSecret || _validInstance != this || ((!_sessionSecret->token.empty() && _sessionSecret->expiration - offset < chrono::system_clock::to_time_t(chrono::system_clock::now())) || _sessionSecret->secret.empty()))
        return false;
    return true;
}

void AWS::getSecret()
{
    if (!_secret || _validInstance != this) {
        unique_lock lock(_mutex);
        _secret = _globalSecret;
        _validInstance = this;
    }
    if (_settings.zonal && (!_sessionSecret || _validInstance != this)) {
        unique_lock lock(_mutex);
        _sessionSecret = _globalSessionSecret;
        _validInstance = this;
    }
}

unique_ptr<utils::DataVector<uint8_t>> AWS::resignRequest(const utils::DataVector<uint8_t>& data, const uint8_t* bodyData, uint64_t bodyLength) const
{
    if (!validKeys() || (_settings.zonal && !validSession()))
        return nullptr;

    auto header = network::HttpRequest::deserialize(string_view(reinterpret_cast<const char*>(data.cdata()), data.size()));
    for (auto it = header.headers.begin(); it != header.headers.end();) {
        if (it->first != "Range" && it->first != "Content-Length")
            it = header.headers.erase(it);
        else
            it++;
    }
    return buildRequest(header, bodyData, bodyLength);
}

// 占位符实现
Provider::Instance AWS::getInstanceDetails(myblob::network::TaskedSendReceiverHandle& sendReceiver)
{
    return Provider::Instance{};
}

string AWS::getInstanceRegion(myblob::network::TaskedSendReceiverHandle& sendReceiver)
{
    return "";
}

void AWS::initCache(myblob::network::TaskedSendReceiverHandle& sendReceiverHandle)
{
}

void AWS::initSecret(myblob::network::TaskedSendReceiverHandle& sendReceiverHandle)
{
}

unique_ptr<utils::DataVector<uint8_t>> AWS::downloadIAMUser() const
{
    return nullptr;
}

unique_ptr<utils::DataVector<uint8_t>> AWS::downloadSecret(string_view content, string& iamUser)
{
    return nullptr;
}

bool AWS::updateSecret(string_view content, string_view iamUser)
{
    return false;
}

bool AWS::updateSessionToken(string_view content)
{
    return false;
}

unique_ptr<utils::DataVector<uint8_t>> AWS::getSessionToken(string_view type) const
{
    return nullptr;
}

string AWS::getIAMAddress()
{
    return "169.254.169.254";
}

uint32_t AWS::getIAMPort()
{
    return 80;
}

}

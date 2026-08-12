#include "cloud/azure.hpp"
#include "cloud/azure_signer.hpp"
#include "network/http_helper.hpp"
#include "network/http_request.hpp"
#include "utils/data_vector.hpp"
#include "utils/utils.hpp"
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace myblob::cloud {

using namespace std;

// --- helper: serialize HttpRequest to DataVector (header only) ---
static unique_ptr<utils::DataVector<uint8_t>> serializeRequest(
    const network::HttpRequest& request)
{
    string raw;
    raw += network::HttpRequest::getRequestMethod(request.method);
    raw += " " + request.path + " ";
    raw += string(network::HttpRequest::getRequestType(request.type)) + "\r\n";
    for (const auto& h : request.headers)
        raw += h.first + ": " + h.second + "\r\n";
    raw += "\r\n";
    auto data = make_unique<utils::DataVector<uint8_t>>(raw.size());
    memcpy(data->data(), raw.data(), raw.size());
    return data;
}

// --- helper: serialize HttpRequest + body into single DataVector ---
static unique_ptr<utils::DataVector<uint8_t>> serializeRequestWithBody(
    const network::HttpRequest& request, string_view body)
{
    string raw;
    raw += network::HttpRequest::getRequestMethod(request.method);
    raw += " " + request.path + " ";
    raw += string(network::HttpRequest::getRequestType(request.type)) + "\r\n";
    for (const auto& h : request.headers)
        raw += h.first + ": " + h.second + "\r\n";
    raw += "\r\n";
    auto totalSize = raw.size() + body.size();
    auto data = make_unique<utils::DataVector<uint8_t>>(totalSize);
    memcpy(data->data(), raw.data(), raw.size());
    memcpy(data->data() + raw.size(), body.data(), body.size());
    return data;
}

static string buildXMSDate() {
    stringstream s;
    const auto t = chrono::system_clock::to_time_t(chrono::system_clock::now());
    s << put_time(gmtime(&t), "%a, %d %b %Y %H:%M:%S GMT");
    return s.str();
}

void Azure::initKey() {
}

static std::unique_ptr<utils::DataVector<uint8_t>> buildAzureInstanceRequest() {
    string httpHeader = "GET /metadata/instance?api-version=2021-02-01 HTTP/1.1\r\nHost: 169.254.169.254\r\nMetadata: true\r\n\r\n";
    return make_unique<utils::DataVector<uint8_t>>(
        reinterpret_cast<uint8_t*>(httpHeader.data()),
        reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}

unique_ptr<utils::DataVector<uint8_t>> Azure::getRequest(
    const string& filePath,
    const pair<uint64_t, uint64_t>& range) const
{
    if (!_secret) {
        return nullptr;
    }
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::GET;
    request.type = network::HttpRequest::Type::HTTP_1_1;
    request.path = "/" + _settings.container + "/" + filePath;
    request.headers.emplace("x-ms-date", testEnvironment ? fakeXMSTimestamp : buildXMSDate());
    request.headers.emplace("Host", getAddress());

    if (range.first != range.second) {
        stringstream rangeString;
        rangeString << "bytes=" << range.first << "-" << range.second;
        request.headers.emplace("Range", rangeString.str());
    }

    request.path = AzureSigner::createSignedRequest(
        _secret->accountName,
        _secret->privateKey,
        request);

    return serializeRequest(request);
}

unique_ptr<utils::DataVector<uint8_t>> Azure::putRequest(
    const string& filePath,
    string_view object) const
{
    if (!_secret) {
        return nullptr;
    }
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::PUT;
    request.type = network::HttpRequest::Type::HTTP_1_1;
    request.path = "/" + _settings.container + "/" + filePath;

    request.headers.emplace("x-ms-date", testEnvironment ? fakeXMSTimestamp : buildXMSDate());
    request.headers.emplace("x-ms-blob-type", "BlockBlob");
    request.headers.emplace("Host", getAddress());
    request.headers.emplace("Content-Length", to_string(object.size()));

    request.path = AzureSigner::createSignedRequest(
        _secret->accountName,
        _secret->privateKey,
        request);

    // 零拷贝：只返回请求头。请求体通过 OriginalMessage::putData 单独发送，
    // 避免 128MB 的内存拷贝。
    return serializeRequest(request);
}
unique_ptr<utils::DataVector<uint8_t>> Azure::deleteRequest(
    const string& filePath) const
{
    if (!_secret) {
        return nullptr;
    }
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::DELETE;
    request.type = network::HttpRequest::Type::HTTP_1_1;
    request.path = "/" + _settings.container + "/" + filePath;

    request.headers.emplace("x-ms-date", testEnvironment ? fakeXMSTimestamp : buildXMSDate());
    request.headers.emplace("Host", getAddress());

    request.path = AzureSigner::createSignedRequest(
        _secret->accountName,
        _secret->privateKey,
        request);

    return serializeRequest(request);
}

string Azure::getAddress() const
{
    if (_secret && !_secret->accountName.empty()) {
        return _secret->accountName + ".blob.core.windows.net";
    }
    return address_;  // fallback to Provider's stored address
}

uint16_t Azure::getPort() const
{
    return _settings.port ? _settings.port : 443;
}

Provider::Instance Azure::getInstanceDetails(
    network::TaskedSendReceiverHandle& sendReceiver)
{
    return {"eastus", "", "Standard_DS2_v2", "azure-vm", getAddress(), getPort()};
}

string Azure::getInstanceRegion(network::TaskedSendReceiverHandle& sendReceiver)
{
    return "eastus";
}

void Azure::initSecret(myblob::network::TaskedSendReceiverHandle& sendReceiverHandle) {
    if (!_secret) {
        _secret = std::make_unique<Secret>();
        // Extract account name from endpoint address (e.g. "myaccount.blob.core.windows.net" → "myaccount")
        if (!address_.empty()) {
            auto dotPos = address_.find('.');
            _secret->accountName = (dotPos != string::npos)
                ? address_.substr(0, dotPos) : address_;
        }
        _secret->privateKey.clear();
    }
}

void Azure::getSecret() {
}

unique_ptr<utils::DataVector<uint8_t>> Azure::putRequestGeneric(
    const string& filePath,
    string_view object,
    uint16_t part,
    string_view uploadId) const
{
    if (!_secret) {
        return nullptr;
    }
    // Non-multipart call: delegate to putRequest
    if (part == 0) {
        return putRequest(filePath, object);
    }
    // Multipart block upload: PUT /container/blob?comp=block&blockid=base64(block-N)
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::PUT;
    request.type = network::HttpRequest::Type::HTTP_1_1;
    request.path = "/" + _settings.container + "/" + filePath;

    // Generate block ID (base64-encoded sequential number)
    stringstream blockIdStream;
    blockIdStream << "block-" << setw(8) << setfill('0') << part;
    string blockId = blockIdStream.str();
    string encodedBlockId = utils::base64Encode(
        reinterpret_cast<const uint8_t*>(blockId.data()), blockId.size());
    request.queries.emplace("comp", "block");
    request.queries.emplace("blockid", encodedBlockId);

    request.headers.emplace("x-ms-date", testEnvironment ? fakeXMSTimestamp : buildXMSDate());
    request.headers.emplace("x-ms-blob-type", "BlockBlob");
    request.headers.emplace("Host", getAddress());
    request.headers.emplace("Content-Length", to_string(object.size()));

    request.path = AzureSigner::createSignedRequest(
        _secret->accountName,
        _secret->privateKey,
        request);

    // 零拷贝：只返回请求头。请求体通过 OriginalMessage::putData 单独发送，
    // 避免 128MB 的内存拷贝。
    return serializeRequest(request);
}
unique_ptr<utils::DataVector<uint8_t>> Azure::createMultiPartRequest(
    const string& filePath) const
{
    // Azure Blob Storage doesn't have an "initiate multipart upload" API like S3.
    // Instead, blocks are uploaded individually via PutBlock, then committed
    // via PutBlockList. This method returns a minimal HEAD-like request whose
    // response lets the Transaction framework proceed; the returned uploadId
    // will be empty since Azure doesn't use upload IDs.
    if (!_secret) {
        return nullptr;
    }
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::GET;
    request.type = network::HttpRequest::Type::HTTP_1_1;
    request.path = "/" + _settings.container + "/" + filePath;
    request.headers.emplace("x-ms-date", testEnvironment ? fakeXMSTimestamp : buildXMSDate());
    request.headers.emplace("Host", getAddress());

    request.path = AzureSigner::createSignedRequest(
        _secret->accountName,
        _secret->privateKey,
        request);

    return serializeRequest(request);
}

unique_ptr<utils::DataVector<uint8_t>> Azure::completeMultiPartRequest(
    const string& filePath,
    string_view uploadId,
    const vector<string>& etags,
    string& content) const
{
    if (!_secret) {
        return nullptr;
    }
    content = "<BlockList>\n";
    for (const auto& etag : etags) {
        content += "<Latest>" + etag + "</Latest>\n";
    }
    content += "</BlockList>\n";

    network::HttpRequest request;
    request.method = network::HttpRequest::Method::POST;
    request.type = network::HttpRequest::Type::HTTP_1_1;
    request.path = "/" + _settings.container + "/" + filePath;
    request.queries.emplace("comp", "blocklist");
    if (!uploadId.empty()) {
        request.queries.emplace("uploadId", uploadId);
    }

    request.headers.emplace("x-ms-date", testEnvironment ? fakeXMSTimestamp : buildXMSDate());
    request.headers.emplace("Content-Type", "application/xml");
    request.headers.emplace("Host", getAddress());
    request.headers.emplace("Content-Length", to_string(content.size()));

    request.path = AzureSigner::createSignedRequest(
        _secret->accountName,
        _secret->privateKey,
        request);

    return serializeRequestWithBody(request, content);
}

unique_ptr<utils::DataVector<uint8_t>> Azure::resignRequest(
    const utils::DataVector<uint8_t>& data,
    const uint8_t* bodyData,
    uint64_t bodyLength) const
{
    auto result = make_unique<utils::DataVector<uint8_t>>(data.size());
    memcpy(static_cast<void*>(result->data()), data.cdata(), data.size());
    return result;
}

unique_ptr<utils::DataVector<uint8_t>> Azure::downloadInstanceInfo() const
{
    return buildAzureInstanceRequest();
}

}
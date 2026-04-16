#include "cloud/azure.hpp"
#include "cloud/azure_signer.hpp"
#include "network/http_helper.hpp"
#include "network/http_request.hpp"
#include "utils/data_vector.hpp"
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace myblob::cloud {

using namespace std;

static string buildXMSDate() {
    stringstream s;
    const auto t = chrono::system_clock::to_time_t(chrono::system_clock::now());
    s << put_time(gmtime(&t), "%a, %d %b %Y %H:%M:%S GMT");
    return s.str();
}

void Azure::initKey() {
}

unique_ptr<utils::DataVector<uint8_t>> Azure::getRequest(
    const string& filePath,
    const pair<uint64_t, uint64_t>& range) const
{
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::GET;
    request.type = network::HttpRequest::Type::HTTP_1_1;
    request.path = "/" + _settings.container + "/" + filePath;
    request.headers.emplace("Host", getAddress());
    request.headers.emplace("x-ms-date", testEnviornment ? fakeXMSTimestamp : buildXMSDate());
    request.headers.emplace("x-ms-version", "2020-04-08");

    if (range.first != range.second) {
        stringstream rangeString;
        rangeString << "bytes=" << range.first << "-" << range.second;
        request.headers.emplace("Range", rangeString.str());
    }

    string authHeader = AzureSigner::createSignedRequest(
        _secret->accountName,
        _secret->privateKey,
        request);

    string httpHeader = network::HttpRequest::getRequestMethod(request.method);
    httpHeader += " " + request.path + " ";
    httpHeader += string(network::HttpRequest::getRequestType(request.type)) + "\r\n";

    for (const auto& h : request.headers) {
        httpHeader += h.first + ": " + h.second + "\r\n";
    }
    httpHeader += "Authorization: " + authHeader + "\r\n";
    httpHeader += "\r\n";

    auto data = make_unique<utils::DataVector<uint8_t>>(httpHeader.size());
    memcpy(static_cast<void*>(data->data()), httpHeader.data(), httpHeader.size());
    return data;
}

unique_ptr<utils::DataVector<uint8_t>> Azure::putRequest(
    const string& filePath,
    string_view object) const
{
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::PUT;
    request.type = network::HttpRequest::Type::HTTP_1_1;
    request.path = "/" + _settings.container + "/" + filePath;

    request.headers.emplace("Host", getAddress());
    request.headers.emplace("x-ms-date",
        testEnviornment ? fakeXMSTimestamp : buildXMSDate());
    request.headers.emplace("x-ms-version", "2020-04-08");
    request.headers.emplace("x-ms-blob-type", "BlockBlob");
    request.headers.emplace("Content-Length", to_string(object.size()));

    string authHeader = AzureSigner::createSignedRequest(
        _secret->accountName,
        _secret->privateKey,
        request);

    string httpHeader = network::HttpRequest::getRequestMethod(request.method);
    httpHeader += " " + request.path + " ";
    httpHeader += string(network::HttpRequest::getRequestType(request.type)) + "\r\n";

    for (const auto& h : request.headers) {
        httpHeader += h.first + ": " + h.second + "\r\n";
    }
    httpHeader += "Authorization: " + authHeader + "\r\n";
    httpHeader += "\r\n";

    auto totalSize = httpHeader.size() + object.size();
    auto data = make_unique<utils::DataVector<uint8_t>>(totalSize);
    memcpy(static_cast<void*>(data->data()), httpHeader.data(), httpHeader.size());
    memcpy(static_cast<void*>(data->data() + httpHeader.size()), object.data(), object.size());
    return data;
}

unique_ptr<utils::DataVector<uint8_t>> Azure::deleteRequest(
    const string& filePath) const
{
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::DELETE;
    request.type = network::HttpRequest::Type::HTTP_1_1;
    request.path = "/" + _settings.container + "/" + filePath;

    request.headers.emplace("Host", getAddress());
    request.headers.emplace("x-ms-date",
        testEnviornment ? fakeXMSTimestamp : buildXMSDate());
    request.headers.emplace("x-ms-version", "2020-04-08");

    string authHeader = AzureSigner::createSignedRequest(
        _secret->accountName,
        _secret->privateKey,
        request);

    string httpHeader = network::HttpRequest::getRequestMethod(request.method);
    httpHeader += " " + request.path + " ";
    httpHeader += string(network::HttpRequest::getRequestType(request.type)) + "\r\n";

    for (const auto& h : request.headers) {
        httpHeader += h.first + ": " + h.second + "\r\n";
    }
    httpHeader += "Authorization: " + authHeader + "\r\n";
    httpHeader += "\r\n";

    auto data = make_unique<utils::DataVector<uint8_t>>(httpHeader.size());
    memcpy(static_cast<void*>(data->data()), httpHeader.data(), httpHeader.size());
    return data;
}

string Azure::getAddress() const
{
    return _secret->accountName + ".blob.core.windows.net";
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
}

void Azure::getSecret() {
}

unique_ptr<utils::DataVector<uint8_t>> Azure::putRequestGeneric(
    const string& filePath,
    string_view object,
    uint16_t part,
    string_view uploadId) const
{
    return putRequest(filePath, object);
}

unique_ptr<utils::DataVector<uint8_t>> Azure::createMultiPartRequest(
    const string& filePath) const
{
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::POST;
    request.type = network::HttpRequest::Type::HTTP_1_1;
    request.path = "/" + _settings.container + "/" + filePath + "?restype=container&comp=blob";

    request.headers.emplace("Host", getAddress());
    request.headers.emplace("x-ms-date", testEnviornment ? fakeXMSTimestamp : buildXMSDate());
    request.headers.emplace("x-ms-version", "2020-04-08");

    string authHeader = AzureSigner::createSignedRequest(
        _secret->accountName,
        _secret->privateKey,
        request);

    string httpHeader = network::HttpRequest::getRequestMethod(request.method);
    httpHeader += " " + request.path + " ";
    httpHeader += string(network::HttpRequest::getRequestType(request.type)) + "\r\n";

    for (const auto& h : request.headers) {
        httpHeader += h.first + ": " + h.second + "\r\n";
    }
    httpHeader += "Authorization: " + authHeader + "\r\n";
    httpHeader += "\r\n";

    auto data = make_unique<utils::DataVector<uint8_t>>(httpHeader.size());
    memcpy(static_cast<void*>(data->data()), httpHeader.data(), httpHeader.size());
    return data;
}

unique_ptr<utils::DataVector<uint8_t>> Azure::completeMultiPartRequest(
    const string& filePath,
    string_view uploadId,
    const vector<string>& etags,
    string& content) const
{
    string httpHeader = "POST /" + _settings.container + "/" + filePath + "?comp=blocklist&uploadId=" + string(uploadId) + " HTTP/1.1\r\n";
    httpHeader += "Host: " + getAddress() + "\r\n";
    httpHeader += "x-ms-date: " + string(testEnviornment ? fakeXMSTimestamp : buildXMSDate()) + "\r\n";
    httpHeader += "x-ms-version: 2020-04-08\r\n";
    httpHeader += "Content-Type: application/xml\r\n";
    httpHeader += "Authorization: SharedKey " + _secret->accountName + ":" + "\r\n";
    httpHeader += "\r\n";

    auto data = make_unique<utils::DataVector<uint8_t>>(httpHeader.size());
    memcpy(static_cast<void*>(data->data()), httpHeader.data(), httpHeader.size());
    return data;
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
    return nullptr;
}

}
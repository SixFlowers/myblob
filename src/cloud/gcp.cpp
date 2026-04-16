#include "cloud/gcp.hpp"
#include "cloud/gcp_signer.hpp"
#include "network/http_helper.hpp"
#include "network/http_request.hpp"
#include "utils/data_vector.hpp"
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace myblob::cloud {

using namespace std;

static string buildAMZTimestamp()
{
    stringstream s;
    const auto t = chrono::system_clock::to_time_t(chrono::system_clock::now());
    s << put_time(gmtime(&t), "%Y%m%dT%H%M%SZ");
    return s.str();
}

unique_ptr<utils::DataVector<uint8_t>> GCP::getRequest(
    const string& filePath,
    const pair<uint64_t, uint64_t>& range) const
{
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::GET;
    request.type = network::HttpRequest::Type::HTTP_1_1;

    request.path = "/" + _settings.bucket + "/" + filePath;

    request.headers.emplace("Host", getAddress());
    request.headers.emplace("x-amz-date",
        testEnviornment ? fakeAMZTimestamp : buildAMZTimestamp());

    if (range.first != range.second) {
        stringstream rangeString;
        rangeString << "bytes=" << range.first << "-" << range.second;
        request.headers.emplace("Range", rangeString.str());
    }

    GCPSigner::StringToSign stringToSign = {
        .region = _settings.region,
        .service = "storage",
        .signedHeaders = "host"
    };

    string authHeader = GCPSigner::createSignedRequest(
        _secret->serviceAccountEmail,
        _secret->privateKey,
        request,
        stringToSign);

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

unique_ptr<utils::DataVector<uint8_t>> GCP::putRequestGeneric(
    const string& filePath,
    string_view object,
    uint16_t part,
    string_view uploadId) const
{
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::PUT;
    request.type = network::HttpRequest::Type::HTTP_1_1;

    request.path = "/" + _settings.bucket + "/" + filePath;

    if (part > 0) {
        request.queries.emplace("partNumber", to_string(part));
        request.queries.emplace("uploadId", uploadId);
    }

    request.headers.emplace("Host", getAddress());
    request.headers.emplace("x-amz-date",
        testEnviornment ? fakeAMZTimestamp : buildAMZTimestamp());
    request.headers.emplace("Content-Length", to_string(object.size()));

    GCPSigner::StringToSign stringToSign = {
        .region = _settings.region,
        .service = "storage",
        .signedHeaders = "host"
    };

    string authHeader = GCPSigner::createSignedRequest(
        _secret->serviceAccountEmail,
        _secret->privateKey,
        request,
        stringToSign);

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

unique_ptr<utils::DataVector<uint8_t>> GCP::deleteRequestGeneric(
    const string& filePath,
    string_view uploadId) const
{
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::DELETE;
    request.type = network::HttpRequest::Type::HTTP_1_1;

    request.path = "/" + _settings.bucket + "/" + filePath;

    if (!uploadId.empty()) {
        request.queries.emplace("uploadId", uploadId);
    }

    request.headers.emplace("Host", getAddress());
    request.headers.emplace("x-amz-date",
        testEnviornment ? fakeAMZTimestamp : buildAMZTimestamp());

    GCPSigner::StringToSign stringToSign = {
        .region = _settings.region,
        .service = "storage",
        .signedHeaders = "host"
    };

    string authHeader = GCPSigner::createSignedRequest(
        _secret->serviceAccountEmail,
        _secret->privateKey,
        request,
        stringToSign);

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

unique_ptr<utils::DataVector<uint8_t>> GCP::createMultiPartRequest(
    const string& filePath) const
{
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::POST;
    request.type = network::HttpRequest::Type::HTTP_1_1;

    request.path = "/" + _settings.bucket + "/" + filePath;
    request.queries.emplace("uploads", "");

    request.headers.emplace("Host", getAddress());
    request.headers.emplace("x-amz-date",
        testEnviornment ? fakeAMZTimestamp : buildAMZTimestamp());

    GCPSigner::StringToSign stringToSign = {
        .region = _settings.region,
        .service = "storage",
        .signedHeaders = "host"
    };

    string authHeader = GCPSigner::createSignedRequest(
        _secret->serviceAccountEmail,
        _secret->privateKey,
        request,
        stringToSign);

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

unique_ptr<utils::DataVector<uint8_t>> GCP::completeMultiPartRequest(
    const string& filePath,
    string_view uploadId,
    const vector<string>& etags,
    string& content) const
{
    content = "<CompleteMultipartUpload>\n";
    for (size_t i = 0; i < etags.size(); i++) {
        content += "<Part>\n<PartNumber>";
        content += to_string(i + 1);
        content += "</PartNumber>\n<ETag>\"";
        content += etags[i];
        content += "\"</ETag>\n</Part>\n";
    }
    content += "</CompleteMultipartUpload>\n";

    network::HttpRequest request;
    request.method = network::HttpRequest::Method::POST;
    request.type = network::HttpRequest::Type::HTTP_1_1;

    request.path = "/" + _settings.bucket + "/" + filePath;
    request.queries.emplace("uploadId", uploadId);

    request.headers.emplace("Host", getAddress());
    request.headers.emplace("x-amz-date",
        testEnviornment ? fakeAMZTimestamp : buildAMZTimestamp());
    request.headers.emplace("Content-Length", to_string(content.size()));

    GCPSigner::StringToSign stringToSign = {
        .region = _settings.region,
        .service = "storage",
        .signedHeaders = "host"
    };

    string authHeader = GCPSigner::createSignedRequest(
        _secret->serviceAccountEmail,
        _secret->privateKey,
        request,
        stringToSign);

    string httpHeader = network::HttpRequest::getRequestMethod(request.method);
    httpHeader += " " + request.path + " ";
    httpHeader += string(network::HttpRequest::getRequestType(request.type)) + "\r\n";

    for (const auto& h : request.headers) {
        httpHeader += h.first + ": " + h.second + "\r\n";
    }
    httpHeader += "Authorization: " + authHeader + "\r\n";
    httpHeader += "\r\n";

    auto totalSize = httpHeader.size() + content.size();
    auto data = make_unique<utils::DataVector<uint8_t>>(totalSize);
    memcpy(static_cast<void*>(data->data()), httpHeader.data(), httpHeader.size());
    memcpy(static_cast<void*>(data->data() + httpHeader.size()), content.data(), content.size());
    return data;
}

string GCP::getAddress() const
{
    return "storage.googleapis.com";
}

uint16_t GCP::getPort() const
{
    return _settings.port ? _settings.port : 443;
}

Provider::Instance GCP::getInstanceDetails(
    network::TaskedSendReceiverHandle& sendReceiver)
{
    return {"us-central1", "", "n2-standard-2", "gcp-vm", getAddress(), getPort()};
}

string GCP::getInstanceRegion(network::TaskedSendReceiverHandle& sendReceiver)
{
    return "us-central1";
}

void GCP::initSecret(myblob::network::TaskedSendReceiverHandle& sendReceiverHandle) {
}

void GCP::getSecret() {
}

unique_ptr<utils::DataVector<uint8_t>> GCP::resignRequest(
    const utils::DataVector<uint8_t>& data,
    const uint8_t* bodyData,
    uint64_t bodyLength) const
{
    auto result = make_unique<utils::DataVector<uint8_t>>(data.size());
    memcpy(static_cast<void*>(result->data()), data.cdata(), data.size());
    return result;
}

unique_ptr<utils::DataVector<uint8_t>> GCP::downloadInstanceInfo(
    const string& info) const
{
    return nullptr;
}

}
#include "cloud/gcp.hpp"
#include "cloud/gcp_signer.hpp"
#include "network/http_helper.hpp"
#include "network/http_request.hpp"
#include "utils/data_vector.hpp"
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
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

static string buildAMZTimestamp()
{
    stringstream s;
    const auto t = chrono::system_clock::to_time_t(chrono::system_clock::now());
    s << put_time(gmtime(&t), "%Y%m%dT%H%M%SZ");
    return s.str();
}

static std::unique_ptr<utils::DataVector<uint8_t>> buildGCPInstanceRequest(const string& info)
{
    string httpHeader = "GET /computeMetadata/v1/instance/" + info + " HTTP/1.1\r\nHost: 169.254.169.254\r\nMetadata-Flavor: Google\r\n\r\n";
    return make_unique<utils::DataVector<uint8_t>>(
        reinterpret_cast<uint8_t*>(httpHeader.data()),
        reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size())
    );
}

unique_ptr<utils::DataVector<uint8_t>> GCP::getRequest(
    const string& filePath,
    const pair<uint64_t, uint64_t>& range) const
{
    if (!_secret) {
        return nullptr;
    }
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::GET;
    request.type = network::HttpRequest::Type::HTTP_1_1;

    request.path = "/" + filePath;
    request.queries.emplace("X-Goog-Date", testEnvironment ? fakeAMZTimestamp : buildAMZTimestamp());

    request.headers.emplace("Host", getAddress());
    request.headers.emplace("Content-Length", "0");

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

    try {
    request.path = GCPSigner::createSignedRequest(
        _secret->serviceAccountEmail,
        _secret->privateKey,
        request,
        stringToSign);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] GCP sign failed: " << e.what() << std::endl;
        return nullptr;
    }

    return serializeRequest(request);
}

unique_ptr<utils::DataVector<uint8_t>> GCP::putRequestGeneric(
    const string& filePath,
    string_view object,
    uint16_t part,
    string_view uploadId) const
{
    if (!_secret) {
        return nullptr;
    }
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::PUT;
    request.type = network::HttpRequest::Type::HTTP_1_1;

    request.path = "/" + filePath;

    if (part > 0) {
        request.queries.emplace("partNumber", to_string(part));
        request.queries.emplace("uploadId", uploadId);
    }

    auto date = testEnvironment ? fakeAMZTimestamp : buildAMZTimestamp();
    request.queries.emplace("X-Goog-Date", date);
    request.headers.emplace("Host", getAddress());
    request.headers.emplace("Date", date);
    request.headers.emplace("Content-Length", to_string(object.size()));

    GCPSigner::StringToSign stringToSign = {
        .region = _settings.region,
        .service = "storage",
        .signedHeaders = "host"
    };

    try {
    request.path = GCPSigner::createSignedRequest(
        _secret->serviceAccountEmail,
        _secret->privateKey,
        request,
        stringToSign);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] GCP sign failed: " << e.what() << std::endl;
        return nullptr;
    }

    return serializeRequestWithBody(request, object);
}

unique_ptr<utils::DataVector<uint8_t>> GCP::deleteRequestGeneric(
    const string& filePath,
    string_view uploadId) const
{
    if (!_secret) {
        return nullptr;
    }
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::DELETE;
    request.type = network::HttpRequest::Type::HTTP_1_1;

    request.path = "/" + filePath;

    if (!uploadId.empty()) {
        request.queries.emplace("uploadId", uploadId);
    }

    auto date = testEnvironment ? fakeAMZTimestamp : buildAMZTimestamp();
    request.queries.emplace("X-Goog-Date", date);
    request.headers.emplace("Host", getAddress());
    request.headers.emplace("Content-Length", "0");

    GCPSigner::StringToSign stringToSign = {
        .region = _settings.region,
        .service = "storage",
        .signedHeaders = "host"
    };

    try {
    request.path = GCPSigner::createSignedRequest(
        _secret->serviceAccountEmail,
        _secret->privateKey,
        request,
        stringToSign);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] GCP sign failed: " << e.what() << std::endl;
        return nullptr;
    }

    return serializeRequest(request);
}

unique_ptr<utils::DataVector<uint8_t>> GCP::createMultiPartRequest(
    const string& filePath) const
{
    if (!_secret) {
        return nullptr;
    }
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::POST;
    request.type = network::HttpRequest::Type::HTTP_1_1;

    request.path = "/" + filePath;
    request.queries.emplace("uploads", "");

    auto date = testEnvironment ? fakeAMZTimestamp : buildAMZTimestamp();
    request.queries.emplace("X-Goog-Date", date);
    request.headers.emplace("Host", getAddress());
    request.headers.emplace("Date", date);
    request.headers.emplace("Content-Length", "0");

    GCPSigner::StringToSign stringToSign = {
        .region = _settings.region,
        .service = "storage",
        .signedHeaders = "host"
    };

    try {
    request.path = GCPSigner::createSignedRequest(
        _secret->serviceAccountEmail,
        _secret->privateKey,
        request,
        stringToSign);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] GCP sign failed: " << e.what() << std::endl;
        return nullptr;
    }

    return serializeRequest(request);
}

unique_ptr<utils::DataVector<uint8_t>> GCP::completeMultiPartRequest(
    const string& filePath,
    string_view uploadId,
    const vector<string>& etags,
    string& content) const
{
    if (!_secret) {
        return nullptr;
    }
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

    request.path = "/" + filePath;
    request.queries.emplace("uploadId", uploadId);

    auto date = testEnvironment ? fakeAMZTimestamp : buildAMZTimestamp();
    request.queries.emplace("X-Goog-Date", date);
    request.headers.emplace("Host", getAddress());
    request.headers.emplace("Date", date);
    request.headers.emplace("Content-Length", to_string(content.size()));

    GCPSigner::StringToSign stringToSign = {
        .region = _settings.region,
        .service = "storage",
        .signedHeaders = "host"
    };

    try {
    request.path = GCPSigner::createSignedRequest(
        _secret->serviceAccountEmail,
        _secret->privateKey,
        request,
        stringToSign);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] GCP sign failed: " << e.what() << std::endl;
        return nullptr;
    }

    return serializeRequestWithBody(request, content);
}

string GCP::getAddress() const
{
    return _settings.bucket + ".storage.googleapis.com";
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
    if (!_secret) {
        _secret = std::make_unique<Secret>();
        _secret->serviceAccountEmail = _settings.bucket;
        _secret->privateKey.clear();
    }
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
    return buildGCPInstanceRequest(info);
}

}
#include "cloud/http_provider.hpp"
#include <cstring>
#include <iostream>

namespace myblob::cloud {

HTTPProvider::HTTPProvider(const std::string& addr, uint16_t port, bool https,
                         myblob::network::ConnectionManager& conn_mgr,
                         myblob::network::HttpClient& http_client)
    : Provider(addr, port, conn_mgr, http_client, 
               https ? CloudService::HTTPS : CloudService::HTTP) {
    std::cerr << "[DEBUG] HTTPProvider: Created for " << addr << ":" << port << std::endl;
}

std::unique_ptr<utils::DataVector<uint8_t>> HTTPProvider::getRequest(
    const std::string& filePath,
    const std::pair<uint64_t, uint64_t>& range
) const {
    auto request = std::make_unique<utils::DataVector<uint8_t>>();
    std::string request_str = "GET " + filePath + " HTTP/1.1\r\n";
    request_str += "Host: " + getAddress() + "\r\n";
    request_str += "Connection: close\r\n";
    
    if (range.first != 0 || range.second != 0) {
        std::string range_header = "bytes=" + std::to_string(range.first) + "-";
        if (range.second > 0) {
            range_header += std::to_string(range.second - 1);
        }
        request_str += "Range: " + range_header + "\r\n";
    }
    
    request_str += "\r\n";
    request->resize(request_str.size());
    memcpy(request->data(), request_str.c_str(), request_str.size());
    return request;
}

std::unique_ptr<utils::DataVector<uint8_t>> HTTPProvider::putRequest(
    const std::string& filePath,
    std::string_view object
) const {
    auto request = std::make_unique<utils::DataVector<uint8_t>>();
    std::string request_str = "PUT " + filePath + " HTTP/1.1\r\n";
    request_str += "Host: " + getAddress() + "\r\n";
    request_str += "Content-Length: " + std::to_string(object.size()) + "\r\n";
    request_str += "Connection: close\r\n";
    request_str += "\r\n";
    
    size_t header_size = request_str.size();
    request->resize(header_size + object.size());
    memcpy(request->data(), request_str.c_str(), header_size);
    memcpy(request->data() + header_size, object.data(), object.size());
    return request;
}

std::unique_ptr<utils::DataVector<uint8_t>> HTTPProvider::deleteRequest(
    const std::string& filePath
) const {
    auto request = std::make_unique<utils::DataVector<uint8_t>>();
    std::string request_str = "DELETE " + filePath + " HTTP/1.1\r\n";
    request_str += "Host: " + getAddress() + "\r\n";
    request_str += "Connection: close\r\n";
    request_str += "\r\n";
    
    size_t header_size = request_str.size();
    request->resize(header_size);
    memcpy(request->data(), request_str.c_str(), header_size);
    return request;
}

// HTTPProvider不支持多部分上传，返回nullptr
std::unique_ptr<utils::DataVector<uint8_t>> HTTPProvider::putRequestGeneric(
    const std::string& filePath,
    std::string_view object,
    uint16_t part,
    std::string_view uploadId
) const {
    // HTTPProvider不支持多部分上传，直接调用putRequest
    return putRequest(filePath, object);
}

std::unique_ptr<utils::DataVector<uint8_t>> HTTPProvider::createMultiPartRequest(
    const std::string& filePath
) const {
    // HTTPProvider不支持多部分上传
    return nullptr;
}

std::unique_ptr<utils::DataVector<uint8_t>> HTTPProvider::completeMultiPartRequest(
    const std::string& filePath,
    std::string_view uploadId,
    const std::vector<std::string>& etags,
    std::string& content
) const {
    // HTTPProvider不支持多部分上传
    return nullptr;
}

std::unique_ptr<utils::DataVector<uint8_t>> HTTPProvider::resignRequest(
    const utils::DataVector<uint8_t>& data,
    const uint8_t* bodyData,
    uint64_t bodyLength
) const {
    // HTTPProvider不需要重新签名
    return std::make_unique<utils::DataVector<uint8_t>>(data);
}

uint64_t HTTPProvider::multipartUploadSize() const {
    // HTTPProvider不支持多部分上传
    return 0;
}

Provider::Instance HTTPProvider::getInstanceDetails(myblob::network::TaskedSendReceiverHandle& sendReceiver) {
    // HTTPProvider不支持获取实例详情
    return Provider::Instance{};
}

void HTTPProvider::initCache(myblob::network::TaskedSendReceiverHandle& sendReceiverHandle) {
    // HTTPProvider不需要初始化缓存
}

void HTTPProvider::initSecret(myblob::network::TaskedSendReceiverHandle& sendReceiverHandle) {
    // HTTPProvider不需要初始化密钥
}

void HTTPProvider::getSecret() {
    // HTTPProvider不需要获取密钥
}

}  // namespace myblob::cloud

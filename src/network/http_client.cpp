#include "network/http_client.hpp"
#include "network/http_helper.hpp"
#include "utils/defer.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <iostream>
#include <chrono>
#include <thread>

namespace myblob::network {

HttpClient::HttpClient() : request_counter_(0) {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    ssl_context_ = SSL_CTX_new(TLS_client_method());
    if (!ssl_context_) {
        throw std::runtime_error("Failed to create SSL context");
    }
}

HttpClient::~HttpClient() {
    if (ssl_context_) {
        SSL_CTX_free(static_cast<SSL_CTX*>(ssl_context_));
    }
}

bool HttpClient::useTls(const std::string& url) {
    return (url.substr(0, 5) == "https");
}

std::string HttpClient::extractHost(const std::string& url) {
    size_t start = url.find("://") + 3;
    size_t end = url.find("/", start);
    std::string host_port = url.substr(start, end - start);
    size_t colon = host_port.find(":");
    if (colon != std::string::npos) {
        return host_port.substr(0, colon);
    }
    return host_port;
}

std::string HttpClient::extractPath(const std::string& url) {
    size_t start = url.find("://") + 3;
    size_t end = url.find("/", start);
    if (end == std::string::npos) {
        return "/";
    }
    return url.substr(end);
}

uint16_t HttpClient::extractPort(const std::string& url) {
    size_t start = url.find("://") + 3;
    size_t end = url.find("/", start);
    std::string host_port = url.substr(start, end - start);
    size_t colon = host_port.find(":");
    if (colon != std::string::npos) {
        return static_cast<uint16_t>(std::stoi(host_port.substr(colon + 1)));
    }
    return useTls(url) ? 443 : 80;
}

int HttpClient::createSocket(const std::string& host, uint16_t port) {
    // 使用 getaddrinfo 替代已废弃且非线程安全的 gethostbyname
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      // 支持 IPv4 + IPv6
    hints.ai_socktype = SOCK_STREAM;

    std::string portStr = std::to_string(port);
    struct addrinfo* result = nullptr;
    int ret = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
    if (ret != 0 || result == nullptr) {
        std::cerr << "[ERROR] DNS resolution failed for " << host
                  << ": " << gai_strerror(ret) << std::endl;
        return -1;
    }
    // RAII cleanup
    auto dnsGuard = myblob::utils::Defer([result]() { freeaddrinfo(result); });

    int sockfd = ::socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sockfd < 0) {
        return -1;
    }

    if (::connect(sockfd, result->ai_addr, result->ai_addrlen) < 0) {
        ::close(sockfd);
        return -1;
    }
    
    return sockfd;
}

HttpResponse HttpClient::doSendRequest(int sockfd, const HttpRequest& request) {
    HttpResponse response;
    
    std::string request_str = "GET " + request.path + " HTTP/1.1\r\n";
    request_str += "Host: test\r\n";
    request_str += "Connection: close\r\n";
    request_str += "\r\n";
    
    ssize_t sent = ::send(sockfd, request_str.c_str(), request_str.size(), 0);
    if (sent < 0) {
        response.code = HttpResponse::Code::INTERNAL_SERVER_ERROR_500;
        return response;
    }
    
    return recvResponse(sockfd);
}

HttpResponse HttpClient::recvResponse(int sockfd) {
    // 使用共享的 HTTP 响应解析（与状态机版本一致）
    utils::DataVector<uint8_t> buffer(8192);
    uint64_t totalRead = 0;

    while (true) {
        // 确保有空间继续读
        if (totalRead + 4096 > buffer.capacity()) {
            if (!buffer.owned()) break;  // 借用模式不能扩容
            buffer.reserve(buffer.capacity() * 2);
        }
        buffer.resize(totalRead + 4096);
        ssize_t n = ::recv(sockfd, buffer.data() + totalRead,
                          buffer.size() - totalRead, 0);
        if (n <= 0) break;
        totalRead += static_cast<uint64_t>(n);
        buffer.resize(totalRead);

        // 使用共享的 HttpHelper 判断响应是否收完
        std::unique_ptr<HttpHelper::Info> info;
        if (HttpHelper::finished(buffer.data(), totalRead, info)) {
            if (info) return info->response;
            break;
        }
    }

    // 收完或出错后，尝试解析已收到的数据
    if (totalRead > 0) {
        try {
            std::string_view sv(reinterpret_cast<const char*>(buffer.data()), totalRead);
            return HttpResponse::deserialize(sv);
        } catch (const std::exception& e) {
            std::cerr << "[WARN] http_client: response parse failed: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[WARN] http_client: response parse failed (unknown error)" << std::endl;
        }
    }
    HttpResponse resp;
    resp.code = HttpResponse::Code::INTERNAL_SERVER_ERROR_500;
    return resp;
}

HttpResponse HttpClient::sendRequest(std::unique_ptr<Connection>& conn,
                                   const std::string& method,
                                   const std::string& path,
                                   uint64_t offset,
                                   uint64_t length) {
    HttpResponse response;
    
    if (!conn->isConnected()) {
        if (!conn->connect()) {
            response.code = HttpResponse::Code::INTERNAL_SERVER_ERROR_500;
            return response;
        }
    }
    
    int sockfd = conn->getSocket();

    // 构建 HTTP 请求字符串
    std::string request_str = method + " " + path + " HTTP/1.1\r\n";
    request_str += "Host: " + conn->getHost() + "\r\n";
    request_str += "Connection: close\r\n";

    if (offset > 0 || length > 0) {
        std::string range = "bytes=" + std::to_string(offset) + "-";
        if (length > 0) {
            range += std::to_string(offset + length - 1);
        }
        request_str += "Range: " + range + "\r\n";
    }

    request_str += "\r\n";

    // 纯 TCP send/recv：TLS 加解密由 HTTPSMessage/TLSConnection 层负责
    ::send(sockfd, request_str.c_str(), request_str.size(), 0);

    return recvResponse(sockfd);
}

HttpResponse HttpClient::get(const std::string& url) {
    std::string host = extractHost(url);
    std::string path = extractPath(url);
    uint16_t port = extractPort(url);
    return get(host, path, port);
}

HttpResponse HttpClient::get(const std::string& host, const std::string& path, uint16_t port) {
    HttpResponse response;
    
    int sockfd = createSocket(host, port);
    if (sockfd < 0) {
        response.code = HttpResponse::Code::INTERNAL_SERVER_ERROR_500;
        return response;
    }
    
    std::string request_str = "GET " + path + " HTTP/1.1\r\n";
    request_str += "Host: " + host + "\r\n";
    request_str += "Connection: close\r\n";
    request_str += "\r\n";
    
    ::send(sockfd, request_str.c_str(), request_str.size(), 0);
    response = recvResponse(sockfd);
    ::close(sockfd);
    
    return response;
}

HttpResponse HttpClient::get(const std::string& host, const std::string& path,
                            uint16_t port, const std::string& ca_file) {
    HttpResponse response;
    
    int sockfd = createSocket(host, port);
    if (sockfd < 0) {
        response.code = HttpResponse::Code::INTERNAL_SERVER_ERROR_500;
        return response;
    }
    
    SSL* ssl = SSL_new(static_cast<SSL_CTX*>(ssl_context_));
    SSL_set_fd(ssl, sockfd);
    
    if (SSL_connect(ssl) != 1) {
        SSL_free(ssl);
        ::close(sockfd);
        response.code = HttpResponse::Code::INTERNAL_SERVER_ERROR_500;
        return response;
    }
    
    std::string request_str = "GET " + path + " HTTP/1.1\r\n";
    request_str += "Host: " + host + "\r\n";
    request_str += "Connection: close\r\n";
    request_str += "\r\n";
    
    SSL_write(ssl, request_str.c_str(), request_str.size());
    
    response = recvResponse(sockfd);
    
    SSL_free(ssl);
    ::close(sockfd);
    
    return response;
}

HttpResponse HttpClient::download(const std::string& url, uint64_t offset, uint64_t length) {
    return get(url);
}

HttpResponse HttpClient::downloadWithRetry(const std::string& url, uint64_t offset,
                                          uint64_t length, int maxRetries, int timeoutSeconds) {
    HttpResponse lastResponse;
    
    for (int attempt = 1; attempt <= maxRetries; attempt++) {
        std::cerr << "[DEBUG] Download attempt " << attempt << "/" << maxRetries << std::endl;
        
        HttpResponse response = download(url, offset, length);
        
        if (HttpResponse::checkSuccess(response.code)) {
            std::cerr << "[DEBUG] Download succeeded on attempt " << attempt << std::endl;
            return response;
        }
        
        lastResponse = response;
        int status = static_cast<int>(HttpResponse::getResponseCodeNumber(response.code));
        
        if (status >= 500) {
            std::cerr << "[WARN] Server error " << status
                      << ", retrying in " << attempt << " seconds..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(attempt));
        } else if (status == 429) {
            std::cerr << "[WARN] Rate limited (429), waiting 5 seconds..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(5));
        } else {
            std::cerr << "[ERROR] Non-retryable error, HTTP status: " << status << std::endl;
            break;
        }
    }
    
    std::cerr << "[ERROR] All " << maxRetries << " attempts failed" << std::endl;
    return lastResponse;
}

}  // namespace myblob::network

#include "network/http_client.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <openssl/ssl.h>
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
    int sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return -1;
    }
    
    struct hostent* server = gethostbyname(host.c_str());
    if (server == nullptr) {
        ::close(sockfd);
        return -1;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
    addr.sin_port = htons(port);
    
    if (::connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
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
    HttpResponse response;
    utils::DataVector<uint8_t> buffer(8192);
    std::string header_buffer;
    bool headers_complete = false;
    size_t content_length = 0;
    size_t body_received = 0;
    
    while (true) {
        ssize_t n = ::recv(sockfd, buffer.data() + header_buffer.size(),
                          buffer.capacity() - header_buffer.size() - 1, 0);
        if (n <= 0) {
            break;
        }
        
        header_buffer.append((const char*)buffer.data() + header_buffer.size(), n);
        
        if (!headers_complete) {
            size_t header_end = header_buffer.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                std::string headers = header_buffer.substr(0, header_end);
                size_t status_pos = headers.find("HTTP/");
                if (status_pos != std::string::npos) {
                    size_t status_end = headers.find("\r\n", status_pos);
                    std::string status_line = headers.substr(status_pos, status_end - status_pos);
                    
                    if (status_line.find("HTTP/1.0") == status_pos) {
                        response.type = HttpResponse::Type::HTTP_1_0;
                    } else {
                        response.type = HttpResponse::Type::HTTP_1_1;
                    }
                    
                    size_t code_pos = status_line.find(" ");
                    if (code_pos != std::string::npos) {
                        int code = std::stoi(status_line.substr(code_pos + 1, 3));
                        switch (code) {
                            case 200: response.code = HttpResponse::Code::OK_200; break;
                            case 201: response.code = HttpResponse::Code::CREATED_201; break;
                            case 204: response.code = HttpResponse::Code::NO_CONTENT_204; break;
                            case 206: response.code = HttpResponse::Code::PARTIAL_CONTENT_206; break;
                            case 400: response.code = HttpResponse::Code::BAD_REQUEST_400; break;
                            case 401: response.code = HttpResponse::Code::UNAUTHORIZED_401; break;
                            case 403: response.code = HttpResponse::Code::FORBIDDEN_403; break;
                            case 404: response.code = HttpResponse::Code::NOT_FOUND_404; break;
                            case 500: response.code = HttpResponse::Code::INTERNAL_SERVER_ERROR_500; break;
                            default: response.code = HttpResponse::Code::UNKNOWN; break;
                        }
                    }
                }
                
                size_t cl_pos = headers.find("Content-Length:");
                if (cl_pos != std::string::npos) {
                    size_t cl_end = headers.find("\r\n", cl_pos);
                    if (cl_end != std::string::npos) {
                        std::string cl_str = headers.substr(cl_pos + 15, cl_end - cl_pos - 15);
                        content_length = stoul(cl_str);
                    }
                }
                
                std::string remaining = header_buffer.substr(header_end + 4);
                header_buffer.clear();
                headers_complete = true;
                body_received = remaining.size();
                if (content_length > 0 && body_received >= content_length) {
                    break;
                }
            }
        } else {
            body_received += n;
            header_buffer.clear();
            if (content_length > 0 && body_received >= content_length) {
                break;
            }
        }
    }
    
    return response;
}

HttpResponse HttpClient::sendRequest(std::shared_ptr<Connection> conn,
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
    bool use_tls = conn->usesTLS();
    
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
    
    if (use_tls) {
        SSL* ssl = conn->getSSL();
        if (ssl) {
            SSL_write(ssl, request_str.c_str(), request_str.size());
        }
    } else {
        ::send(sockfd, request_str.c_str(), request_str.size(), 0);
    }
    
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

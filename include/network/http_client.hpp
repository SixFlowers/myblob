#pragma once
#include "../utils/data_vector.hpp"
#include "../utils/defer.hpp"
#include "connection.hpp"
#include "http_response.hpp"
#include "http_request.hpp"
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace myblob::network {

class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    
    HttpResponse get(const std::string& url);
    HttpResponse get(const std::string& host, const std::string& path, uint16_t port = 80);
    HttpResponse get(const std::string& host, const std::string& path, uint16_t port,
                     const std::string& ca_file);
    
    HttpResponse download(const std::string& url, uint64_t offset = 0, uint64_t length = 0);
    
    HttpResponse downloadWithRetry(const std::string& url, uint64_t offset = 0,
                                   uint64_t length = 0, int maxRetries = 3,
                                   int timeoutSeconds = 5);
    
    HttpResponse sendRequest(std::shared_ptr<Connection> conn,
                             const std::string& method,
                             const std::string& path,
                             uint64_t offset = 0,
                             uint64_t length = 0);
    
private:
    int createSocket(const std::string& host, uint16_t port);
    
    HttpResponse doSendRequest(int sockfd, const HttpRequest& request);
    
    HttpResponse recvResponse(int sockfd);
    
    bool useTls(const std::string& url);
    
    std::string extractHost(const std::string& url);
    
    std::string extractPath(const std::string& url);
    
    uint16_t extractPort(const std::string& url);
    
    void* ssl_context_ = nullptr;
    std::atomic<uint64_t> request_counter_{0};
};

}  // namespace myblob::network

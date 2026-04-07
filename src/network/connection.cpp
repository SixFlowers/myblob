#include "network/connection.hpp"
#include "network/connection_manager.hpp"
#include "utils/defer.hpp"
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <iostream>

namespace myblob::network {

Connection::Connection(const std::string& host, uint16_t port, bool use_tls)
    : host_(host)
    , port_(port)
    , use_tls_(use_tls)
    , sockfd_(-1)
    , ssl_(nullptr)
    , ssl_context_(nullptr)
    , state_(ConnectionState::IDLE)
    , last_used_(time(nullptr))
    , connected_(false) {
    std::cerr << "[DEBUG] Connection: Creating for " << host_ << ":" << port_ 
              << " (TLS=" << use_tls_ << ")" << std::endl;
}

Connection::~Connection() {
    disconnect();
}

bool Connection::connect() {
    std::cerr << "[DEBUG] Connection::connect: " << host_ << ":" << port_ << std::endl;
    
    if (state_.load() == ConnectionState::CONNECTING) {
        std::cerr << "[WARN] Connection already connecting" << std::endl;
        return false;
    }
    
    if (sockfd_ > 0 && connected_) {
        return true;
    }
    
    state_.store(ConnectionState::CONNECTING);
    
    sockfd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_ < 0) {
        std::cerr << "[DEBUG] socket() failed" << std::endl;
        state_.store(ConnectionState::CLOSED);
        return false;
    }
    
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(sockfd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    int flag = 1;
    setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    
    struct hostent* server = gethostbyname(host_.c_str());
    if (server == nullptr) {
        std::cerr << "[ERROR] DNS lookup failed for: " << host_ << std::endl;
        state_.store(ConnectionState::CLOSED);
        ::close(sockfd_);
        sockfd_ = -1;
        return false;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
    addr.sin_port = htons(port_);
    
    if (::connect(sockfd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[DEBUG] connect() failed" << std::endl;
        state_.store(ConnectionState::CLOSED);
        ::close(sockfd_);
        sockfd_ = -1;
        return false;
    }
    
    std::cerr << "[DEBUG] TCP connected to " << host_ << ":" << port_ << std::endl;
    
    if (use_tls_) {
        ssl_ = SSL_new(static_cast<SSL_CTX*>(ssl_context_));
        if (!ssl_) {
            std::cerr << "[ERROR] SSL_new() failed" << std::endl;
            state_.store(ConnectionState::CLOSED);
            ::close(sockfd_);
            sockfd_ = -1;
            return false;
        }
        SSL_set_fd(ssl_, sockfd_);
        if (SSL_connect(ssl_) != 1) {
            std::cerr << "[ERROR] SSL connection failed" << std::endl;
            SSL_free(ssl_);
            ssl_ = nullptr;
            state_.store(ConnectionState::CLOSED);
            ::close(sockfd_);
            sockfd_ = -1;
            return false;
        }
        std::cerr << "[DEBUG] TLS handshake successful" << std::endl;
    }
    
    connected_ = true;
    state_.store(ConnectionState::IDLE);
    last_used_ = time(nullptr);
    
    std::cerr << "[DEBUG] Connection::connect: SUCCESS" << std::endl;
    return true;
}

void Connection::disconnect() {
    std::cerr << "[DEBUG] Connection::disconnect: " << host_ << ":" << port_ << std::endl;
    
    if (ssl_) {
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    
    if (sockfd_ >= 0) {
        ::close(sockfd_);
        sockfd_ = -1;
    }
    
    connected_ = false;
    state_.store(ConnectionState::CLOSED);
}

void Connection::setTCPSettings(const TCPSettings& settings) {
    tcpSettings_ = settings;
}

}  // namespace myblob::network

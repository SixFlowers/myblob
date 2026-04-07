#include "network/connection_manager.hpp"
#include "utils/defer.hpp"
#include <openssl/ssl.h>
#include <algorithm>
#include <iostream>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>

namespace myblob::network {

ConnectionManager::ConnectionManager(
    size_t max_connections,
    int max_idle_seconds,
    int connect_timeout)
    : max_connections_(max_connections)
    , max_idle_seconds_(max_idle_seconds)
    , connect_timeout_(connect_timeout)
    , ssl_context_(nullptr)
    , stop_(false),
    defaultSettings_(),pollSocket_(std::make_unique<PollSocket>()) {
    std::cerr << "[DEBUG] ConnectionManager: Initializing..." << std::endl;
    
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    ssl_context_ = SSL_CTX_new(TLS_client_method());
    if (!ssl_context_) {
        throw std::runtime_error("Failed to create SSL context");
    }
    
    std::cerr << "[DEBUG] ConnectionManager: Initialized (max_connections=" 
              << max_connections_ << ")" << std::endl;
}

ConnectionManager::~ConnectionManager() {
    closeAll();
    
    if (ssl_context_) {
        SSL_CTX_free(static_cast<SSL_CTX*>(ssl_context_));
        ssl_context_ = nullptr;
    }
    
    std::cerr << "[DEBUG] ConnectionManager: Destroyed" << std::endl;
}

std::shared_ptr<Connection> ConnectionManager::getConnection(
    const std::string& host,
    uint16_t port,
    bool use_tls) {
    std::cerr << "[DEBUG] ConnectionManager::getConnection: " << host << ":" << port 
              << " (TLS=" << use_tls << ")" << std::endl;
    
    std::unique_lock<std::mutex> lock(mutex_);
    
    cond_.wait(lock, [this]() {
        if (stop_) {
            return true;
        }
        for (auto& conn : pool_) {
            if (conn->getState() == ConnectionState::IDLE) {
                return true;
            }
        }
        return pool_.size() < max_connections_;
    });
    
    if (stop_) {
        std::cerr << "[WARN] ConnectionManager is stopped" << std::endl;
        return nullptr;
    }
    
    auto conn = findExistingConnection(host, port, use_tls);
    if (conn) {
        std::cerr << "[DEBUG] Reusing existing connection" << std::endl;
        conn->markUsed();
        return conn;
    }
    
    conn = createNewConnection(host, port, use_tls);
    if (!conn) {
        std::cerr << "[ERROR] Failed to create new connection" << std::endl;
        return nullptr;
    }
    
    pool_.push_back(conn);
    conn->markUsed();
    return conn;
}

void ConnectionManager::returnConnection(std::shared_ptr<Connection> conn) {
    if (!conn) {
        return;
    }
    
    std::cerr << "[DEBUG] ConnectionManager::returnConnection: " << conn->getHost() 
              << ":" << conn->getPort() << std::endl;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (conn->getState() == ConnectionState::CLOSED) {
        auto it = std::find(pool_.begin(), pool_.end(), conn);
        if (it != pool_.end()) {
            pool_.erase(it);
            std::cerr << "[DEBUG] Removed closed connection from pool" << std::endl;
        }
        return;
    }
    
    conn->markIdle();
    cond_.notify_one();
}

void ConnectionManager::closeIdleConnections() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::cerr << "[DEBUG] ConnectionManager::closeIdleConnections: pool size before=" 
              << pool_.size() << std::endl;
    
    for (auto it = pool_.begin(); it != pool_.end();) {
        auto& conn = *it;
        if (conn->isIdleTooLong(max_idle_seconds_)) {
            std::cerr << "[DEBUG] Closing idle connection to " << conn->getHost() << std::endl;
            conn->disconnect();
            it = pool_.erase(it);
        } else {
            ++it;
        }
    }
    
    std::cerr << "[DEBUG] ConnectionManager::closeIdleConnections: pool size after=" 
              << pool_.size() << std::endl;
}

void ConnectionManager::closeAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    stop_ = true;
    cond_.notify_all();
    
    std::cerr << "[DEBUG] ConnectionManager::closeAll: closing " << pool_.size()
              << " connections" << std::endl;
    
    for (auto& conn : pool_) {
        conn->disconnect();
    }
    
    pool_.clear();
}

ConnectionManager::Stats ConnectionManager::getStats() const {
    Stats stats;
    stats.total_connections = pool_.size();
    stats.max_connections = max_connections_;
    stats.idle_connections = 0;
    stats.in_use_connections = 0;
    
    for (const auto& conn : pool_) {
        if (conn->getState() == ConnectionState::IDLE) {
            stats.idle_connections++;
        } else if (conn->getState() == ConnectionState::IN_USE) {
            stats.in_use_connections++;
        }
    }
    
    return stats;
}

std::shared_ptr<Connection> ConnectionManager::findExistingConnection(
    const std::string& host,
    uint16_t port,
    bool use_tls) {
    for (auto& conn : pool_) {
        if (conn->getHost() == host && 
            conn->getPort() == port &&
            conn->getState() == ConnectionState::IDLE &&
            conn->usesTLS() == use_tls) {
            return conn;
        }
    }
    return nullptr;
}

std::shared_ptr<Connection> ConnectionManager::createNewConnection(
    const std::string& host,
    uint16_t port,
    bool use_tls) {
    int sockfd = ::socket(AF_INET,SOCK_STREAM,0);
    if(sockfd < 0){
        return nullptr;
    }
    TCPSettings settings;
    applyTCPSettings(sockfd,settings);
    auto conn = std::make_shared<Connection>(host, port, use_tls);
    conn->setSocket(sockfd);
    if (use_tls) {
        conn->setSSLContext(ssl_context_);
    }
    if (!conn->connect()) {
        return nullptr;
    }
    return conn;
}
void ConnectionManager::applyTCPSettings(int fd, const TCPSettings& settings) {
    // 非阻塞模式
    if (settings.nonBlocking > 0) {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    
    // TCP_NODELAY（禁用 Nagle 算法）
    if (settings.noDelay > 0) {
        setsockopt(fd, SOL_TCP, TCP_NODELAY, &settings.noDelay, sizeof(settings.noDelay));
    }
    
    // SO_KEEPALIVE
    if (settings.keepAlive > 0) {
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &settings.keepAlive, sizeof(settings.keepAlive));
    }
    
    // TCP_KEEPIDLE
    if (settings.keepIdle > 0) {
        setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, &settings.keepIdle, sizeof(settings.keepIdle));
    }
    
    // TCP_KEEPINTVL
    if (settings.keepIntvl > 0) {
        setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, &settings.keepIntvl, sizeof(settings.keepIntvl));
    }
    
    // TCP_KEEPCNT
    if (settings.keepCnt > 0) {
        setsockopt(fd, SOL_TCP, TCP_KEEPCNT, &settings.keepCnt, sizeof(settings.keepCnt));
    }
    
    // SO_RCVBUF
    if (settings.recvBuffer > 0) {
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &settings.recvBuffer, sizeof(settings.recvBuffer));
    }
    
    // SO_REUSEPORT
    if (settings.reusePorts > 0) {
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &settings.reusePorts, sizeof(settings.reusePorts));
    }
    
    // TCP_LINGER2
    if (settings.linger > 0) {
        setsockopt(fd, SOL_TCP, TCP_LINGER2, &settings.linger, sizeof(settings.linger));
    }
    
    // SO_REUSEADDR
    if (settings.reuse > 0) {
        int flag = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    }
}
}  // namespace myblob::network

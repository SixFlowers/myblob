#pragma once
#include "connection.hpp"
#include "network/socket.hpp"
#include "network/poll_socket.hpp"
#include "network/tcp_settings.hpp"
#include "network/cache.hpp"
#include <memory>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>
#ifdef MYBLOB_HAS_IO_URING
#include "network/io_uring_socket.hpp"
#endif
namespace myblob::network {
class ThroughputCache;
class ConnectionManager {
public:
    explicit ConnectionManager(
        size_t max_connections = 100,
        int max_idle_seconds = 300,
        int connect_timeout = 10
    );
    
    ~ConnectionManager();
    
    ConnectionManager(const ConnectionManager&) = delete;
    ConnectionManager& operator=(const ConnectionManager&) = delete;
    ConnectionManager(ConnectionManager&&) = delete;
    ConnectionManager& operator=(ConnectionManager&&) = delete;

    std::shared_ptr<Connection> getConnection(
        const std::string& host,
        uint16_t port,
        bool use_tls
    );
    
    void returnConnection(std::shared_ptr<Connection> conn);
    void closeIdleConnections();
    void closeAll();
    
    struct Stats {
        size_t total_connections;
        size_t idle_connections;
        size_t in_use_connections;
        size_t max_connections;
    };
    
    Stats getStats() const;
    
    PollSocket& getPollSocket();
    bool isUsingIoUring() const {
        return usingIoUring_;
    }
    Socket& getSocket() { return *socket_; }
    void enableThroughputCache();

private:
    std::shared_ptr<Connection> findExistingConnection(
        const std::string& host,
        uint16_t port,
        bool use_tls
    );
    
    std::shared_ptr<Connection> createNewConnection(
        const std::string& host,
        uint16_t port,
        bool use_tls
    );
    
    void applyTCPSettings(int fd, const TCPSettings& settings);
    
    std::vector<std::shared_ptr<Connection>> pool_;
    size_t max_connections_;
    int max_idle_seconds_;
    int connect_timeout_;
    mutable std::mutex mutex_;
    void* ssl_context_;
    std::atomic<bool> stop_;
    std::condition_variable cond_;
    //std::unique_ptr<PollSocket> pollSocket_;//poll多路复用器
    TCPSettings defaultSettings_;//默认TCP配置
    std::unique_ptr<Socket> socket_;
    std::unique_ptr<PollSocket> fallbackPollSocket_;  // io_uring 模式下的备用 PollSocket
    std::unique_ptr<Cache> cache_;
    bool usingIoUring_ = false;
};

}  // namespace myblob::network

#pragma once
#include "connection.hpp"
#include <memory>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>

namespace myblob::network {

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
    
    std::vector<std::shared_ptr<Connection>> pool_;
    size_t max_connections_;
    int max_idle_seconds_;
    int connect_timeout_;
    mutable std::mutex mutex_;
    void* ssl_context_;
    std::atomic<bool> stop_;
    std::condition_variable cond_;
};

}  // namespace myblob::network

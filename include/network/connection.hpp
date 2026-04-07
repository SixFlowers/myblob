#pragma once
#include <cstdint>
#include <string>
#include <atomic>
#include <openssl/ssl.h>
#include <ctime>
#include "tcp_settings.hpp"

namespace myblob::network {

enum class ConnectionState {
    IDLE,
    IN_USE,
    CONNECTING,
    CLOSED
};

class Connection {
public:
    Connection(const std::string& host, uint16_t port, bool use_tls);
    ~Connection();
    
    Connection(const Connection& con) = delete;
    Connection& operator=(const Connection& con) = delete;
    Connection(Connection&&) = delete;
    Connection& operator=(Connection&&) = delete;
    
    bool connect();
    void disconnect();
    
    int getSocket() const { return sockfd_; }
    
    SSL* getSSL() const { return ssl_; }
    
    bool isConnected() const { return connected_; }
    
    ConnectionState getState() const { return state_.load(); }
    
    void setState(ConnectionState s) { state_.store(s); }
    
    void markUsed() {
        state_.store(ConnectionState::IN_USE);
        last_used_ = time(nullptr);
    }
    
    void markIdle() {
        state_.store(ConnectionState::IDLE);
    }
    
    const std::string& getHost() const { return host_; }
    
    uint16_t getPort() const { return port_; }
    
    bool usesTLS() const { return use_tls_; }
    
    void setSSLContext(void* ctx) { ssl_context_ = ctx; }
    
    bool isIdleTooLong(int max_idle_seconds) const {
        if (state_.load() != ConnectionState::IDLE) {
            return false;
        }
        time_t now = time(nullptr);
        return (now - last_used_) > max_idle_seconds;
    }
    
    void setSocket(int fd) {
        sockfd_ = fd;
    }
    
    void setTCPSettings(const TCPSettings& settings);
    
    const TCPSettings& getTCPSettings() const { return tcpSettings_; }

private:
    TCPSettings tcpSettings_;
    std::string host_;
    uint16_t port_;
    bool use_tls_;
    int sockfd_;
    SSL* ssl_;
    void* ssl_context_;
    std::atomic<ConnectionState> state_;
    time_t last_used_;
    bool connected_;
};

}  // namespace myblob::network

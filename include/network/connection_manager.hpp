#pragma once
#include "connection.hpp"
#include "network/socket.hpp"
#include "network/poll_socket.hpp"
#include "network/tcp_settings.hpp"
#include "network/cache.hpp"
#include "network/tls_context.hpp"
#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
#ifdef MYBLOB_HAS_IO_URING
#include "network/io_uring_socket.hpp"
#endif

namespace myblob::network {
class ThroughputCache;

/// 连接池管理器：管理所有 TCP/TLS 连接的创建、复用、回收和清理
///
/// 单线程设计：每个 daemon 线程拥有独立的 ConnectionManager，
/// 无跨线程竞争，不需要锁或 shared_ptr。
///
/// 工作流程:
///   HTTPMessage 需要发送请求
///     → getConnection(host, port)    从池中取/创建连接
///     → 使用连接发送/接收数据
///     → returnConnection(conn)       归还，标记 IDLE 等待复用
///     → closeIdleConnections()       定期清理空闲太久的连接
class ConnectionManager {
public:
    explicit ConnectionManager(
        size_t max_connections = 100,
        int max_idle_seconds = 300,
        int connect_timeout = 10
    );

    ~ConnectionManager();

    // 禁止拷贝和移动
    ConnectionManager(const ConnectionManager&) = delete;
    ConnectionManager& operator=(const ConnectionManager&) = delete;
    ConnectionManager(ConnectionManager&&) = delete;
    ConnectionManager& operator=(ConnectionManager&&) = delete;

    /// 从连接池获取/创建连接（单线程，无锁）
    /// - 优先从 idle_ map O(1) 查找匹配连接
    /// - 其次扫描池中 nullptr 空槽
    /// - 没有则创建新连接
    std::unique_ptr<Connection> getConnection(
        const std::string& host,
        uint16_t port,
        bool use_tls
    );

    /// 归还连接到池中（标记为 IDLE，放入 idle_ map 等待复用）
    void returnConnection(std::unique_ptr<Connection> conn);

    /// 清理空闲时间过长的连接
    void closeIdleConnections();

    /// 关闭所有连接
    void closeAll();

    /// 连接池统计信息
    struct Stats {
        size_t total_connections;
        size_t idle_connections;
        size_t in_use_connections;
        size_t max_connections;
    };

    Stats getStats() const;

    PollSocket& getPollSocket();

    bool isUsingIoUring() const { return usingIoUring_; }

    Socket& getSocket() { return *socket_; }

    TLSContext& getTLSContext() { return *tlsContext_; }

    void enableThroughputCache();
    bool hasCache() const { return cache_ != nullptr; }

private:
    /// 生成 idle_ 的查找 key
    static std::string makeKey(const std::string& host, uint16_t port, bool use_tls) {
        return host + ":" + std::to_string(port) + (use_tls ? ":tls" : "");
    }

    /// 创建新连接
    std::unique_ptr<Connection> createNewConnection(
        const std::string& host,
        uint16_t port,
        bool use_tls
    );

    void applyTCPSettings(int fd, const TCPSettings& settings);

    /// pool_: 所有 Connection（IDLE 的留在 idle_ map 中，IN_USE 的槽位变为 nullptr）
    std::vector<std::unique_ptr<Connection>> pool_;
    /// idle_: 空闲连接，key = "host:port:tls"，O(1) 查找
    std::unordered_map<std::string, std::unique_ptr<Connection>> idle_;
    size_t max_connections_;
    int max_idle_seconds_;
    int connect_timeout_;
    std::atomic<bool> stop_;
    TCPSettings defaultSettings_;
    std::unique_ptr<TLSContext> tlsContext_;
    std::unique_ptr<Socket> socket_;
    std::unique_ptr<PollSocket> fallbackPollSocket_;
    std::unique_ptr<Cache> cache_;
    bool usingIoUring_ = false;
};

}  // namespace myblob::network

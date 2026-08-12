#include "network/connection_manager.hpp"
#include "network/throughput_cache.hpp"
#include "utils/defer.hpp"
#include <algorithm>
#include <iostream>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <mutex>
#include <unistd.h>

namespace myblob::network {

ConnectionManager::ConnectionManager(
    size_t max_connections,
    int max_idle_seconds,
    int connect_timeout)
    : max_connections_(max_connections)
    , max_idle_seconds_(max_idle_seconds)
    , connect_timeout_(connect_timeout)
    , stop_(false)
    , defaultSettings_()
    , tlsContext_(std::make_unique<TLSContext>())
    , socket_(nullptr)
    , usingIoUring_(false)
{
#ifdef MYBLOB_HAS_IO_URING
    try {
        socket_ = std::make_unique<IOUringSocket>(1024);
        usingIoUring_ = true;
    } catch (const std::exception& e) {
        socket_ = std::make_unique<PollSocket>();
        usingIoUring_ = false;
    }
#else
    socket_ = std::make_unique<PollSocket>();
    usingIoUring_ = false;
#endif
}

ConnectionManager::~ConnectionManager() {
    closeAll();
}

/// 单线程获取连接：O(1) 从 idle_ map 查找，其次扫描 pool_ 空槽
std::unique_ptr<Connection> ConnectionManager::getConnection(
    const std::string& host,
    uint16_t port,
    bool use_tls)
{
    if (stop_) {
        return nullptr;
    }

    // 第1步：从 idle_ map O(1) 查找匹配的空闲连接
    auto key = makeKey(host, port, use_tls);
    auto it = idle_.find(key);
    if (it != idle_.end()) {
        auto conn = std::move(it->second);
        idle_.erase(it);
        conn->markUsed();
        return conn;
    }

    // 第2步：扫描 pool_ 中的 nullptr 空槽（连接被取走时留下的）
    for (auto& slot : pool_) {
        if (slot && slot->getState() == ConnectionState::IDLE
            && slot->getHost() == host
            && slot->getPort() == port
            && slot->usesTLS() == use_tls)
        {
            auto conn = std::move(slot);  // slot 变为 nullptr
            conn->markUsed();
            return conn;
        }
    }

    // 第3步：池未满则创建新连接（DNS + TCP 握手，单线程不阻塞任何人）
    if (pool_.size() < max_connections_) {
        auto conn = createNewConnection(host, port, use_tls);
        if (conn) {
            conn->markUsed();
            pool_.push_back(nullptr);  // 占位（连接在工作，不在池中）
        }
        return conn;
    }

    // 池满：扫描所有槽位，清理已断开的连接腾空间
    for (auto it = pool_.begin(); it != pool_.end(); ++it) {
        if (*it && (*it)->getState() == ConnectionState::CLOSED) {
            it = pool_.erase(it);
            --it;
        }
    }
    if (pool_.size() < max_connections_) {
        auto conn = createNewConnection(host, port, use_tls);
        if (conn) {
            conn->markUsed();
            pool_.push_back(nullptr);
        }
        return conn;
    }

    return nullptr;
}

/// 归还连接：如果连接完好，放入 idle_ map（O(1) 下次查找）；否则丢弃
void ConnectionManager::returnConnection(std::unique_ptr<Connection> conn) {
    if (!conn) {
        return;
    }

    if (conn->getState() == ConnectionState::CLOSED || !conn->isConnected()) {
        // 连接已断，从 pool_ 移除对应槽位
        for (auto it = pool_.begin(); it != pool_.end(); ++it) {
            if (!*it) {
                it = pool_.erase(it);
                return;  // 找到并删除空槽
            }
        }
        return;
    }

    // 连接完好：标记 IDLE，放入 idle_ map
    conn->markIdle();
    auto key = makeKey(conn->getHost(), conn->getPort(), conn->usesTLS());
    // 如果 idle_ 已有同 key 的连接，旧的先放回 pool_ 空槽
    auto oldIt = idle_.find(key);
    if (oldIt != idle_.end()) {
        // 旧连接放回 pool_ 空槽或直接丢弃
        bool placed = false;
        for (auto& slot : pool_) {
            if (!slot) {
                slot = std::move(oldIt->second);
                placed = true;
                break;
            }
        }
        idle_.erase(oldIt);
        if (!placed) {
            // pool_ 满，丢弃旧连接
            oldIt->second.reset();
        }
    }
    idle_[key] = std::move(conn);
}

void ConnectionManager::closeIdleConnections() {
    // 清理 idle_ map 中过期连接
    for (auto it = idle_.begin(); it != idle_.end();) {
        if (it->second && it->second->isIdleTooLong(max_idle_seconds_)) {
            it->second->disconnect();
            it = idle_.erase(it);
        } else {
            ++it;
        }
    }
    // 清理 pool_ 中 IDLE 过期的连接
    for (auto& slot : pool_) {
        if (slot && slot->getState() == ConnectionState::IDLE
            && slot->isIdleTooLong(max_idle_seconds_))
        {
            slot->disconnect();
            slot.reset();
        }
    }
}

void ConnectionManager::closeAll() {
    stop_ = true;
    for (auto& kv : idle_) {
        if (kv.second) kv.second->disconnect();
    }
    idle_.clear();
    for (auto& conn : pool_) {
        if (conn) conn->disconnect();
    }
    pool_.clear();
}

ConnectionManager::Stats ConnectionManager::getStats() const {
    Stats stats{};
    stats.max_connections = max_connections_;
    stats.idle_connections = idle_.size();
    stats.in_use_connections = 0;
    stats.total_connections = idle_.size();

    for (const auto& conn : pool_) {
        if (conn) {
            stats.total_connections++;
            if (conn->getState() == ConnectionState::IDLE) {
                stats.idle_connections++;
            } else if (conn->getState() == ConnectionState::IN_USE) {
                stats.in_use_connections++;
            }
        }
    }
    return stats;
}

std::unique_ptr<Connection> ConnectionManager::createNewConnection(
    const std::string& host,
    uint16_t port,
    bool use_tls)
{
    auto conn = std::make_unique<Connection>(host, port, use_tls);
    if (!conn->connect()) {
        return nullptr;
    }
    applyTCPSettings(conn->getSocket(), defaultSettings_);
    return conn;
}

void ConnectionManager::enableThroughputCache() {
    cache_ = std::make_unique<ThroughputCache>();
}

void ConnectionManager::applyTCPSettings(int fd, const TCPSettings& settings) {
    if (settings.nonBlocking > 0) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags != -1) {
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
    }

    if (settings.noDelay > 0) {
        setsockopt(fd, SOL_TCP, TCP_NODELAY, &settings.noDelay, sizeof(settings.noDelay));
    }

    if (settings.keepAlive > 0) {
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &settings.keepAlive, sizeof(settings.keepAlive));
    }

    if (settings.keepIdle > 0) {
        setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, &settings.keepIdle, sizeof(settings.keepIdle));
    }

    if (settings.keepIntvl > 0) {
        setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, &settings.keepIntvl, sizeof(settings.keepIntvl));
    }

    if (settings.keepCnt > 0) {
        setsockopt(fd, SOL_TCP, TCP_KEEPCNT, &settings.keepCnt, sizeof(settings.keepCnt));
    }

    if (settings.recvBuffer > 0) {
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &settings.recvBuffer, sizeof(settings.recvBuffer));
    }

    if (settings.reusePorts > 0) {
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &settings.reusePorts, sizeof(settings.reusePorts));
    }

    if (settings.linger > 0) {
        setsockopt(fd, SOL_TCP, TCP_LINGER2, &settings.linger, sizeof(settings.linger));
    }

    if (settings.reuse > 0) {
        int flag = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    }
}

PollSocket& ConnectionManager::getPollSocket() {
    if (!usingIoUring_) {
        return *static_cast<PollSocket*>(socket_.get());
    }
    if (!fallbackPollSocket_) {
        static std::mutex fallbackMutex;
        std::lock_guard<std::mutex> lock(fallbackMutex);
        if (!fallbackPollSocket_) {
            fallbackPollSocket_ = std::make_unique<PollSocket>();
        }
    }
    return *fallbackPollSocket_;
}

}  // namespace myblob::network

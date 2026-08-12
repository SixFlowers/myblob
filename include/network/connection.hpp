#pragma once
#include <cstdint>
#include <string>
#include <atomic>
#include <openssl/ssl.h>
#include <ctime>
#include "tcp_settings.hpp"

namespace myblob::network {

//连接状态枚举，描述一个 TCP/TLS 连接的生命周期阶段
enum class ConnectionState {
    IDLE,        //空闲：连接已建立，当前无人使用，可被复用
    IN_USE,      //使用中：某个 HTTPMessage 正在通过此连接收发数据
    CONNECTING,  //连接中：正在执行 TCP 三次握手 / TLS 握手
    CLOSED       //已关闭：连接断开，不可用
};

//单个 TCP/TLS 连接，代表一个到 S3/Azure/GCP 服务器的 socket 连接
//生命周期：创建 → connect() → markUsed()/markIdle() 循环复用 → disconnect()
//由 ConnectionManager 管理的连接池中的基本单元
class Connection {
public:
    /**
     * 构造函数：指定目标地址和是否使用 TLS，但不立即连接（需要调 connect()）
     * @param host 目标主机名（如 my-bucket.s3.amazonaws.com）
     * @param port 目标端口（HTTP=80, HTTPS=443, MinIO=9001 等）
     * @param use_tls 是否使用 TLS（HTTPS）
     */
    Connection(const std::string& host, uint16_t port, bool use_tls);
    
    /**
     * 析构函数：自动 disconnect()，释放 socket 和 SSL 资源
     */
    ~Connection();
    
    //禁止拷贝和移动：连接是独占资源（socket fd 不能被两个对象持有），不能被复制或转移
    Connection(const Connection& con) = delete;
    Connection& operator=(const Connection& con) = delete;
    Connection(Connection&&) = delete;
    Connection& operator=(Connection&&) = delete;
    
    /**
     * 建立 TCP 连接（如果 use_tls=true 还会做 TLS 握手）
     * @return true=连接成功，false=失败
     */
    //返回 true=连接成功，false=失败
    bool connect();
    //关闭连接：释放 SSL 和 socket
    void disconnect();
    
    //获取底层 socket 文件描述符，用于 Socket::send()/recv()
    int getSocket() const { return sockfd_; }
    
    //获取 OpenSSL SSL 对象，用于 TLS 加密通信（HTTPS 场景）
    SSL* getSSL() const { return ssl_; }
    
    //连接是否已建立
    bool isConnected() const { return connected_; }
    
    //获取/设置连接状态（原子操作，线程安全）
    ConnectionState getState() const { return state_.load(); }
    void setState(ConnectionState s) { state_.store(s); }
    
    //标记为"使用中"：从连接池取出时调用
    void markUsed() {
        state_.store(ConnectionState::IN_USE, std::memory_order_release);
        last_used_.store(time(nullptr), std::memory_order_release);
    }

    //标记为"空闲"：归还连接池时调用
    void markIdle() {
        state_.store(ConnectionState::IDLE, std::memory_order_release);
    }
    
    const std::string& getHost() const { return host_; }
    
    uint16_t getPort() const { return port_; }
    
    bool usesTLS() const { return use_tls_; }
    
    //判断空闲时间是否超过阈值，用于连接池清理过期连接
    // 线程安全：last_used_ 是 atomic，state_ 也是 atomic
    bool isIdleTooLong(int max_idle_seconds) const {
        if (state_.load(std::memory_order_acquire) != ConnectionState::IDLE) {
            return false;//非空闲状态不检查
        }
        time_t now = time(nullptr);
        return (now - last_used_.load(std::memory_order_acquire)) > max_idle_seconds;
    }
    
    //设置/获取 TCP 配置参数（keepalive、nodelay 等）
    void setTCPSettings(const TCPSettings& settings);
    const TCPSettings& getTCPSettings() const { return tcpSettings_; }

private:
    TCPSettings tcpSettings_;              //TCP 参数配置
    std::string host_;                     //目标主机名（如 my-bucket.s3.amazonaws.com）
    uint16_t port_;                        //目标端口（HTTP=80, HTTPS=443, MinIO=9001 等）
    bool use_tls_;                         //是否使用 TLS（HTTPS）
    int sockfd_;                           //socket 文件描述符（-1 表示未创建）
    SSL* ssl_;                             //OpenSSL SSL 对象（nullptr 表示未启用 TLS）
    std::atomic<ConnectionState> state_;  //连接状态（原子变量，多线程安全）
    std::atomic<time_t> last_used_;        //最后使用时间戳（原子变量），用于超时清理
    bool connected_;                       //TCP 连接是否已建立
};

}  // namespace myblob::network

#include "network/connection.hpp"
#include "network/connection_manager.hpp"
#include "utils/defer.hpp"
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <poll.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <iostream>
#include <sstream>

namespace myblob::network {

//构造函数：只记录参数，不立即建立连接（延迟连接模式）
//sockfd_ = -1 表示还没有创建 socket
//ssl_ = nullptr 表示还没有 TLS 上下文
Connection::Connection(const std::string& host, uint16_t port, bool use_tls)
    : host_(host)
    , port_(port)
    , use_tls_(use_tls)
    , sockfd_(-1)          //尚未创建 socket
    , ssl_(nullptr)        //尚未创建 SSL 对象
    , state_(ConnectionState::IDLE)  //初始状态为空闲
    , last_used_(time(nullptr))      //记录创建时间
    , connected_(false) {  //尚未连接
    // Connection: Creating for host:port (TLS=...)
}

//析构函数：自动断开连接，释放资源
Connection::~Connection() {
    disconnect();
}

bool Connection::connect() {
    //防止重复连接：如果正在连接中，直接返回失败
    if (state_.load() == ConnectionState::CONNECTING) {
        return false;
    }
    
    //如果已经连接成功，直接返回 true（连接池复用场景）
    if (sockfd_ > 0 && connected_) {
        return true;
    }
    
    state_.store(ConnectionState::CONNECTING);
    
    //第1步：创建 TCP socket（AF_INET=IPv4, SOCK_STREAM=TCP）
    //Connection 统一负责 socket 创建，不再由外部 setSocket() 传入
    if (sockfd_ < 0) {
        sockfd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    }
    if (sockfd_ < 0) {
        state_.store(ConnectionState::CLOSED);
        return false;
    }
    
    //第2步：DNS 解析（主机名 → IP 地址）支持 IPv4 + IPv6
    //使用 getaddrinfo 替代已废弃且非线程安全的 gethostbyname
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      // 同时支持 IPv4 和 IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP

    std::ostringstream portStr;
    portStr << port_;
    struct addrinfo* result = nullptr;
    int dnsRet = getaddrinfo(host_.c_str(), portStr.str().c_str(), &hints, &result);
    if (dnsRet != 0 || result == nullptr) {
        std::cerr << "[ERROR] DNS resolution failed for " << host_
                  << ": " << gai_strerror(dnsRet) << std::endl;
        state_.store(ConnectionState::CLOSED);
        ::close(sockfd_);
        sockfd_ = -1;
        return false;
    }
    // RAII guard: auto-free result when leaving scope
    myblob::utils::Defer dnsGuard([result]() { freeaddrinfo(result); });

    //第3步：重新创建 socket 根据协议族（IPv4 或 IPv6）
    // 关闭步骤1创建的默认 socket，使用 DNS 返回的地址族重新创建
    ::close(sockfd_);
    sockfd_ = ::socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sockfd_ < 0) {
        state_.store(ConnectionState::CLOSED);
        return false;
    }

    // 设置为非阻塞模式（在 connect 之前）
    int flags = fcntl(sockfd_, F_GETFL, 0);
    fcntl(sockfd_, F_SETFL, flags | O_NONBLOCK);

    //第4步：发起非阻塞 TCP 连接
    auto timeoutMs = static_cast<int>(tcpSettings_.timeout.count());
    int connectRet = ::connect(sockfd_, result->ai_addr, result->ai_addrlen);
    if (connectRet < 0) {
        //EINPROGRESS: 非阻塞 socket 连接正在进行中（正常）
        //EALREADY: 已有连接正在进行中
        if (errno != EINPROGRESS && errno != EALREADY) {
            //其他错误：连接失败
            state_.store(ConnectionState::CLOSED);
            ::close(sockfd_);
            sockfd_ = -1;
            return false;
        }

        //非阻塞连接需要用 poll 等待连接完成
        struct pollfd pfd;
        pfd.fd = sockfd_;
        pfd.events = POLLOUT;   //连接完成时 fd 变为可写
        pfd.revents = 0;
        int pollRes = ::poll(&pfd, 1, timeoutMs);
        if (pollRes <= 0) {
            //超时或 poll 错误
            state_.store(ConnectionState::CLOSED);
            ::close(sockfd_);
            sockfd_ = -1;
            return false;
        }

        //poll 返回后检查 SO_ERROR：连接可能成功也可能失败
        int socketError = 0;
        socklen_t socketErrorLength = sizeof(socketError);
        if (getsockopt(sockfd_, SOL_SOCKET, SO_ERROR, &socketError, &socketErrorLength) < 0 || socketError != 0) {
            //连接失败（如目标主机拒绝连接）
            state_.store(ConnectionState::CLOSED);
            ::close(sockfd_);
            sockfd_ = -1;
            return false;
        }
    }
    
    //第5步：连接成功，更新状态
    connected_ = true;
    state_.store(ConnectionState::IDLE);                 //空闲，等待被使用
    last_used_.store(time(nullptr), std::memory_order_release);  //记录连接时间
    
    return true;
}

void Connection::disconnect() {
    //先释放 SSL 资源（如果有 TLS）
    if (ssl_) {
        SSL_free(ssl_);   //释放 OpenSSL SSL 对象
        ssl_ = nullptr;
    }
    
    //再关闭 socket
    if (sockfd_ >= 0) {
        ::close(sockfd_);  //关闭文件描述符
        sockfd_ = -1;
    }
    
    connected_ = false;
    state_.store(ConnectionState::CLOSED);
}

void Connection::setTCPSettings(const TCPSettings& settings) {
    tcpSettings_ = settings;
}

}  // namespace myblob::network

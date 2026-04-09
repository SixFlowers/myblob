# MyBlob

简化版云存储客户端，对标 [AnyBlob](https://github.com/Dobiasd/anyblob)，用于学习现代 C++ 网络编程。

## 📚 项目介绍

MyBlob 是一个轻量级的云存储客户端实现，旨在通过简化 AnyBlob 的架构，帮助开发者理解云存储 SDK 的核心原理。

### 学习目标

- 理解 Provider 抽象模式
- 掌握 HTTP/HTTPS 协议实现
- 学会连接池和资源管理
- 理解异步任务状态机
- 掌握多路复用 I/O
- 理解 TLS/SSL 加密通信

## ✨ 功能特性

| 特性 | 状态 | 说明 |
|------|------|------|
| Provider 抽象 | ✅ | 统一接口支持多种云服务商 |
| HTTP 请求 | ✅ | 支持 GET/PUT/DELETE |
| 连接池 | ✅ | 复用 TCP 连接，提高性能 |
| 异步任务 | ✅ | MessageState 状态机 |
| 批量请求 | ✅ | Transaction 批量处理 |
| 多路复用 | ✅ | poll/epoll I/O |
| TLS/HTTPS | ✅ | TLSContext、TLSConnection |
| 云服务商认证 | 🔲 | 计划中 |

## 🏗️ 项目结构

```
myblob/
├── include/
│   ├── cloud/                      # 云服务商抽象层
│   │   ├── provider.hpp            # Provider 基类（抽象接口）
│   │   ├── http_provider.hpp        # HTTP Provider 实现
│   │   └── transaction.hpp          # 事务管理器
│   ├── network/                    # 网络层
│   │   ├── http_helper.hpp         # HTTP 工具函数
│   │   ├── http_request.hpp        # HTTP 请求结构
│   │   ├── http_response.hpp       # HTTP 响应结构
│   │   ├── connection.hpp          # TCP 连接封装
│   │   ├── connection_manager.hpp  # 连接池管理
│   │   ├── tls_connection.hpp      # TLS 连接封装
│   │   ├── tls_context.hpp         # TLS 上下文
│   │   ├── message_state.hpp       # 消息状态枚举
│   │   ├── message_result.hpp      # 消息结果
│   │   ├── original_message.hpp    # 原始消息
│   │   └── poll_socket.hpp         # poll 多路复用
│   └── utils/
│       └── data_vector.hpp         # 动态数组容器
├── src/                            # 实现文件
│   ├── cloud/
│   │   ├── provider.cpp
│   │   ├── http_provider.cpp
│   │   └── transaction.cpp
│   └── network/
│       ├── connection.cpp
│       ├── connection_manager.cpp
│       ├── tls_connection.cpp
│       ├── tls_context.cpp
│       ├── http_request.cpp
│       ├── http_response.cpp
│       ├── http_helper.cpp
│       ├── message_result.cpp
│       ├── original_message.cpp
│       └── poll_socket.cpp
└── example/
    ├── sync_example.cpp            # 同步下载示例
    ├── batch_example.cpp            # 批量下载示例
    ├── httpbin_example.cpp         # HTTP 测试示例
    └── httpbin_https_example.cpp   # HTTPS 测试示例
```

## 🔧 编译和运行

### 环境要求

- C++17 或更高
- CMake 3.10+
- OpenSSL
- Linux/macOS

### 编译步骤

```bash
# 1. 创建 build 目录
mkdir build && cd build

# 2. 配置项目
cmake ..

# 3. 编译
make

# 4. 运行示例
./httpbin_example      # 测试 HTTP
./httpbin_https_example # 测试 HTTPS
```

### 运行结果示例

```
=== MyBlob HTTP/HTTPS 测试 ===
测试地址: httpbin.org

添加 GET /get 请求...
添加 GET /ip 请求...
添加 GET /uuid 请求...
共添加了 3 个请求
开始执行...

=== 执行结果 ===
=== /get ===
状态: 7
成功: 是
dataVector_ 大小: 423 字节
response_ length: 423 字节
响应内容:
HTTP/1.1 200 OK
...
```

## 📖 核心概念

### 1. Provider 模式

Provider 是云存储的统一抽象接口：

```cpp
class Provider {
public:
    virtual std::unique_ptr<utils::DataVector<uint8_t>> getRequest(...) = 0;
    virtual std::unique_ptr<utils::DataVector<uint8_t>> putRequest(...) = 0;
    virtual std::unique_ptr<utils::DataVector<uint8_t>> deleteRequest(...) = 0;
};
```

### 2. 异步任务状态机

```
Init → SendingInit → Sending → ReceivingInit → Receiving → Finished
  ↓         ↓           ↓          ↓            ↓
Aborted   Aborted    Aborted   Aborted       Aborted
```

### 3. 多路复用

使用 poll 同时监控多个 socket，避免阻塞：

```cpp
PollSocket pollSocket;
pollSocket.send({fd1, EventType::write});
pollSocket.send({fd2, EventType::write});
pollSocket.submit();  // 等待任意一个就绪
```

### 4. TLS 连接

使用 OpenSSL BIO 进行 TLS 加密通信：

```cpp
TLSConnection tlsConnection;
tlsConnection.connect(connectionManager, request);
```

## 📚 学习路线

| 版本 | 内容 | 状态 |
|------|------|------|
| 版本 1-3 | 基础版：Provider、HTTP、ConnectionManager | ✅ 完成 |
| 版本 4 | 异步任务版：MessageState、MessageResult、OriginalMessage | ✅ 完成 |
| 版本 5 | 多路复用版：Socket、PollSocket、Transaction.execute() | ✅ 完成 |
| 版本 6 | TLS/HTTPS 版：TLSContext、TLSConnection | ✅ 完成 |
| 版本 7 | 任务调度器：TaskedSendReceiver、RingBuffer | 🔲 计划 |
| 版本 8 | 配置与缓存：Config、Cache | 🔲 计划 |
| 版本 9+ | 云服务商认证：AWS、Azure、GCP | 🔲 计划 |

## 🔍 与 AnyBlob 对照

| 组件 | AnyBlob | MyBlob |
|------|---------|--------|
| Provider | ✅ | ✅ |
| ConnectionManager | ✅ | ✅ |
| MessageState | ✅ | ✅ |
| MessageResult | ✅ | ✅ |
| Socket/PollSocket | ✅ | ✅ |
| TLSConnection | ✅ | ✅ |
| TLSContext | ✅ | ✅ |
| TaskedSendReceiver | ✅ | 🔲 |
| 云服务商签名 | ✅ | 🔲 |

## 🧪 测试

使用 httpbin.org 测试：

```bash
# 克隆项目
git clone https://github.com/SixFlowers/myblob.git
cd myblob

# 编译
mkdir build && cd build
cmake .. && make

# 运行 HTTP 测试
./httpbin_example

# 运行 HTTPS 测试
./httpbin_https_example
```

## 📖 参考资料

- [AnyBlob](https://github.com/Dobiasd/anyblob) - 原始项目
- [RFC 9110](https://www.rfc-editor.org/rfc/rfc9110) - HTTP 标准
- [RFC 7540](https://httpwg.org/specs/rfc7540.html) - HTTP/2
- [POSIX Socket](https://man7.org/linux/man-pages/man7/socket.7.html) - Socket API
- [OpenSSL Documentation](https://www.openssl.org/docs/) - TLS/SSL 文档

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

## 📄 License

MIT License - 详见 [LICENSE](LICENSE) 文件

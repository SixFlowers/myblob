# MyBlob

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/std/the-standard)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform](https://img.shields.io/badge/Platform-Linux-green.svg)](https://www.linux.org/)

高性能云存储客户端库，对标 [AnyBlob](https://github.com/AnyBlob/anyblob)，用于学习现代 C++ 网络编程和高性能 I/O。

## 📚 项目介绍

MyBlob 是一个轻量级但功能完整的云存储客户端实现，旨在通过实践理解云存储 SDK 的核心原理和现代 C++ 网络编程技术。

### 核心特性

- **多云服务商支持**: AWS S3, Azure Blob, Google Cloud Storage, MinIO, 以及通用 HTTP/HTTPS
- **高性能 I/O**: 基于 io_uring 的异步 I/O，支持 poll/epoll 多路复用
- **连接池管理**: 智能 TCP 连接复用，减少连接开销
- **完整 HTTP/HTTPS 协议栈**: 自定义 HTTP 消息状态机，支持 TLS/SSL 加密
- **批量操作**: Transaction 事务管理，支持迭代器和多部分上传
- **现代 C++17**: 使用智能指针、移动语义、lambda 表达式等现代特性

## ✨ 功能特性

| 特性 | 状态 | 说明 |
|------|------|------|
| Provider 抽象 | ✅ | 统一接口支持 AWS/Azure/GCP/MinIO/HTTP |
| HTTP/HTTPS 请求 | ✅ | 完整消息状态机实现 |
| 连接池 | ✅ | 复用 TCP 连接，自动清理空闲连接 |
| 异步任务 | ✅ | MessageTask 状态机，支持 HTTP/HTTPS |
| 批量请求 | ✅ | Transaction 批量处理，支持迭代器 |
| 多路复用 | ✅ | poll/epoll/io_uring |
| TLS/SSL | ✅ | TLSConnection, TLSContext |
| 多部分上传 | ✅ | MultipartUpload 支持 |
| 请求重签名 | ✅ | 支持失败重试和重新签名 |
| io_uring | ✅ | Linux 高性能异步 I/O |

## 🏗️ 项目结构

```
myblob/
├── include/
│   ├── cloud/                      # 云服务商抽象层
│   │   ├── provider.hpp            # Provider 基类
│   │   ├── aws.hpp                 # AWS S3 实现
│   │   ├── azure.hpp               # Azure Blob 实现
│   │   ├── gcp.hpp                 # Google Cloud Storage 实现
│   │   ├── minio.hpp               # MinIO 实现
│   │   ├── http_provider.hpp       # HTTP/HTTPS Provider
│   │   └── transaction.hpp         # 事务管理器（含迭代器、多部分上传）
│   ├── network/                    # 网络层
│   │   ├── message_task.hpp        # 消息任务基类
│   │   ├── http_message.hpp        # HTTP 消息状态机
│   │   ├── https_message.hpp       # HTTPS 消息状态机
│   │   ├── connection_manager.hpp  # 连接池管理
│   │   ├── tls_connection.hpp      # TLS 连接封装
│   │   ├── tls_context.hpp         # TLS 上下文
│   │   ├── socket.hpp              # Socket 抽象
│   │   ├── poll_socket.hpp         # poll/epoll 实现
│   │   ├── io_uring_socket.hpp     # io_uring 实现
│   │   ├── http_helper.hpp         # HTTP 工具函数
│   │   ├── http_request.hpp        # HTTP 请求
│   │   ├── http_response.hpp       # HTTP 响应
│   │   ├── message_state.hpp       # 消息状态枚举
│   │   ├── message_result.hpp      # 消息结果
│   │   ├── original_message.hpp    # 原始消息
│   │   ├── tasked_send_receiver.hpp # 任务调度器
│   │   └── config.hpp              # 配置管理
│   └── utils/                      # 工具类
│       ├── data_vector.hpp         # 动态数组容器
│       └── ring_buffer.hpp         # 环形缓冲区
├── src/                            # 实现文件
│   ├── cloud/                      # 云服务商实现
│   └── network/                    # 网络层实现
├── example/                        # 示例程序
│   ├── sync_example.cpp            # 同步下载示例
│   ├── batch_example.cpp           # 批量请求示例
│   ├── httpbin_example.cpp         # HTTP 测试示例
│   ├── cloud_example.cpp           # 多云服务示例
│   └── providers_example.cpp       # 提供商示例
└── build/                          # 构建目录
```

## 🔧 编译和运行

### 环境要求

- C++17 或更高
- CMake 3.10+
- OpenSSL 1.1.1+
- Linux (支持 io_uring)
- g++ 9.0+ 或 clang++ 9.0+

### 依赖安装

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y cmake g++ libssl-dev

# 检查内核是否支持 io_uring (Linux 5.1+)
uname -r
```

### 编译步骤

```bash
# 1. 克隆仓库
git clone https://github.com/yourusername/myblob.git
cd myblob

# 2. 创建 build 目录
mkdir build && cd build

# 3. 配置项目
cmake ..

# 4. 编译
make -j$(nproc)
```

### 运行示例

```bash
# HTTP 测试
./httpbin_example

# 批量请求
./batch_example

# 云服务示例
./cloud_example

# 提供商示例
./providers_example
```

## 📖 使用示例

### 基本 HTTP 请求

```cpp
#include "cloud/provider.hpp"
#include "cloud/transaction.hpp"

// 创建 Provider
auto provider = myblob::cloud::Provider::makeProvider(
    "http://httpbin.org"
);

// 创建事务
myblob::cloud::Transaction txn(provider.get());

// 添加 GET 请求
txn.getObjectRequest("/get", {0, 0}, buffer, sizeof(buffer));

// 执行请求
txn.execute();

// 检查结果
for (auto& msg : txn.getMessages()) {
    if (msg->result.success()) {
        std::cout << "Response: " << msg->result.getResult() << std::endl;
    }
}
```

### 使用迭代器

```cpp
myblob::cloud::Transaction txn(provider.get());
txn.getObjectRequest("/get");
txn.getObjectRequest("/ip");
txn.execute();

// 使用迭代器遍历结果
for (auto& result : txn) {
    if (result.success()) {
        std::cout << "Result: " << result.getResult() << std::endl;
    }
}
```

### 批量异步请求

```cpp
myblob::cloud::Transaction txn(provider.get());

// 添加多个请求
for (int i = 0; i < 10; ++i) {
    txn.getObjectRequest("/get");
}

// 异步执行
txn.executeAsync([]() {
    std::cout << "All requests completed!" << std::endl;
});
```

## 🎯 架构设计

### 核心组件

1. **Provider**: 云服务商抽象基类，定义统一接口
2. **Transaction**: 事务管理器，批量处理请求，支持迭代器
3. **MessageTask**: 消息任务基类，HTTPMessage/HTTPSMessage 实现状态机
4. **ConnectionManager**: 连接池管理，支持连接复用
5. **TaskedSendReceiver**: 任务调度器，管理异步 I/O

### 消息状态机

```
Init -> InitSending -> Sending -> InitReceiving -> Receiving -> Finished
                    |                                    |
                    +------------> Aborted <---------------+
```

### 性能优化

- **io_uring**: Linux 高性能异步 I/O，减少系统调用开销
- **连接池**: 复用 TCP 连接，避免频繁创建/销毁
- **零拷贝**: 使用 DataVector 减少内存拷贝
- **批量处理**: Transaction 批量提交请求，提高吞吐量

## 📊 性能对比

| 操作 | MyBlob | AnyBlob | 说明 |
|------|--------|---------|------|
| 单文件下载 | ✅ | ✅ | 相当 |
| 批量下载 | ✅ | ✅ | 相当 |
| 连接复用 | ✅ | ✅ | 相当 |
| io_uring | ✅ | ✅ | 相当 |
| 内存占用 | 较低 | 较低 | 相当 |

## 📝 学习资源

### 核心概念

- [Provider 抽象模式](docs/provider_pattern.md)
- [HTTP 消息状态机](docs/message_state_machine.md)
- [连接池设计](docs/connection_pool.md)
- [io_uring 原理](docs/iouring.md)

### 代码详解

- [MyBlob最终版代码详解.md](MyBlob最终版代码详解.md)
- [MyBlob缺失功能实现指南.md](MyBlob缺失功能实现指南.md)

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

## 📄 许可证

本项目采用 [MIT 许可证](LICENSE) 开源。

## 🙏 致谢

- 灵感来源于 [AnyBlob](https://github.com/AnyBlob/anyblob)
- 感谢 C++ 社区的优秀开源项目

## 📧 联系

如有问题或建议，欢迎通过 GitHub Issues 联系。

---

**注意**: 本项目主要用于学习和研究目的，生产环境使用请谨慎评估。

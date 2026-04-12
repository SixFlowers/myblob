#include "cloud/provider.hpp"
#include "cloud/http_provider.hpp"
#include "network/connection_manager.hpp"
#include "network/http_client.hpp"
#include "network/http_response.hpp"
#include <iostream>

int main() {
    std::cout << "=== MyBlob io_uring Demo ===" << std::endl;
    
    // 创建连接管理器（自动检测并使用 io_uring）
    myblob::network::ConnectionManager connMgr(100, 300, 10);
    
    // 检查是否使用了 io_uring
    if (connMgr.isUsingIoUring()) {
        std::cout << "✅ Using io_uring for high performance!" << std::endl;
    } else {
        std::cout << "⚠️  Using poll (io_uring not available)" << std::endl;
    }
    
    // 启用吞吐量缓存
    connMgr.enableThroughputCache();
    
    // 创建 HTTP 客户端
    myblob::network::HttpClient http_client;
    
    // 创建 Provider
    myblob::cloud::HTTPProvider provider(
        "example.com",
        80,
        false,
        connMgr,
        http_client
    );
    
    // 下载文件
    std::cout << "\nDownloading..." << std::endl;
    auto response = provider.download("/");
    
    // 使用 HttpResponse 的方法检查响应
    bool success = myblob::network::HttpResponse::checkSuccess(response.code);
    
    if (success) {
        std::cout << "✅ Download successful!" << std::endl;
        std::cout << "Status code: " << myblob::network::HttpResponse::getResponseCode(response.code) << std::endl;
        std::cout << "HTTP version: " << myblob::network::HttpResponse::getResponseType(response.type) << std::endl;
        std::cout << "Headers count: " << response.headers.size() << std::endl;
        for (const auto& [key, value] : response.headers) {
            std::cout << "  " << key << ": " << value << std::endl;
        }
    } else {
        std::cout << "❌ Download failed!" << std::endl;
        std::cout << "Status code: " << myblob::network::HttpResponse::getResponseCode(response.code) << std::endl;
    }
    
    std::cout << "\n=== Demo completed ===" << std::endl;
    
    return 0;
}

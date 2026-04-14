#include "cloud/provider.hpp"
#include "cloud/transaction.hpp"
#include <iostream>
#include <vector>

int main() {
    using namespace myblob;
    
    // 使用 HTTP Provider 测试批量请求功能
    auto provider = cloud::Provider::makeProvider(
        "http://httpbin.org",
        false
    );
    
    if (!provider) {
        std::cerr << "创建 Provider 失败" << std::endl;
        return 1;
    }
    
    // 创建事务
    cloud::Transaction transaction(provider.get());
    
    // 分配缓冲区用于多个请求
    std::vector<uint8_t> buffer1(4096);
    std::vector<uint8_t> buffer2(4096);
    std::vector<uint8_t> buffer3(4096);
    
    // 添加多个 GET 请求（模拟批量下载）
    transaction.getObjectRequest("/get", {0, 0}, buffer1.data(), buffer1.size());
    transaction.getObjectRequest("/get", {0, 0}, buffer2.data(), buffer2.size());
    transaction.getObjectRequest("/get", {0, 0}, buffer3.data(), buffer3.size());
    
    std::cout << "添加了 " << transaction.getMessages().size() << " 个请求" << std::endl;
    std::cout << "开始批量下载..." << std::endl;
    
    // 执行所有请求
    transaction.execute();
    
    // 打印结果
    std::cout << "\n=== 执行结果 ===" << std::endl;
    int reqNum = 1;
    for (auto& msg : transaction.getMessages()) {
        auto& result = msg->result;
        std::cout << "=== 请求 " << reqNum++ << " ===" << std::endl;
        std::cout << "状态: " << static_cast<int>(result.getState()) << std::endl;
        std::cout << "成功: " << (result.success() ? "是" : "否") << std::endl;
        if (result.success()) {
            std::cout << "大小: " << result.getSize() << std::endl;
        }
    }
    
    return 0;
}

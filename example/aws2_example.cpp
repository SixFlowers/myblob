#include "cloud/provider.hpp"
#include "cloud/transaction.hpp"
#include <iostream>

int main() {
    using namespace myblob;
    
    // 使用 HTTPS Provider 测试基本功能
    auto provider = cloud::Provider::makeProvider(
        "https://httpbin.org",
        true  // HTTPS
    );
    
    if (!provider) {
        std::cerr << "创建 Provider 失败" << std::endl;
        return 1;
    }
    
    // 创建事务
    cloud::Transaction transaction(provider.get());
    
    // 分配缓冲区
    uint8_t buffer[4096];
    
    // 添加 GET 请求
    if (transaction.getObjectRequest("/get", {0, 0}, buffer, sizeof(buffer))) {
        std::cout << "请求已添加到事务" << std::endl;
        
        // 执行请求
        transaction.execute();
        
        // 打印结果
        for (auto& msg : transaction.getMessages()) {
            auto& result = msg->result;
            std::cout << "状态: " << static_cast<int>(result.getState()) << std::endl;
            std::cout << "成功: " << (result.success() ? "是" : "否") << std::endl;
            if (result.success()) {
                std::cout << "大小: " << result.getSize() << std::endl;
            } else {
                std::cout << "错误码: " << result.getFailureCode() << std::endl;
            }
        }
    }
    
    return 0;
}

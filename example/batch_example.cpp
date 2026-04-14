#include "cloud/provider.hpp"
#include "cloud/transaction.hpp"
#include "network/message_result.hpp"
#include <iostream>

// ================================================================
// 打印结果的回调函数
// ================================================================
void printResult(const myblob::network::MessageResult& result, const std::string& path) {
    std::cout << "=== " << path << " ===" << std::endl;
    std::cout << "状态: " << static_cast<int>(result.getState()) << std::endl;
    std::cout << "成功: " << (result.success() ? "是" : "否") << std::endl;
    if (result.success()) {
        std::cout << "大小: " << result.getSize() << std::endl;
        auto data = result.getResult();
        if (!data.empty()) {
            std::cout << "响应内容:" << std::endl;
            std::cout.write(data.data(), data.size());
            std::cout << std::endl;
        }
    } else {
        std::cout << "错误码: " << result.getFailureCode() << std::endl;
    }
    std::cout << std::endl;
}

int main() {
    // ================================================================
    // 1. 创建 Provider
    // ================================================================
    auto provider = myblob::cloud::Provider::makeProvider("http://httpbin.org");
    if (!provider) {
        std::cerr << "创建 Provider 失败" << std::endl;
        return 1;
    }
    
    // ================================================================
    // 2. 创建 Transaction
    // ================================================================
    myblob::cloud::Transaction transaction(provider.get());
    
    // 分配缓冲区
    uint8_t buffer1[1024];
    uint8_t buffer2[1024];
    uint8_t buffer3[1024];
    
    // ================================================================
    // 3. 添加多个 GET 请求
    // ================================================================
    transaction.getObjectRequest("/get", {0, 0}, buffer1, sizeof(buffer1));
    //transaction.getObjectRequest("/get", {0, 0}, buffer2, sizeof(buffer2));
    //transaction.getObjectRequest("/get", {0, 0}, buffer3, sizeof(buffer3));
    
    std::cout << "添加了 " << transaction.getMessages().size() << " 个请求" << std::endl;
    std::cout << "开始批量执行..." << std::endl;
    
    // ================================================================
    // 4. 同步执行
    // 使用 poll 多路复用，同时处理多个请求
    // ================================================================
    transaction.execute();
    
    // ================================================================
    // 5. 打印结果
    // ================================================================
    std::cout << "\n=== 执行结果 ===" << std::endl;
    int i = 1;
    for (auto& msg : transaction.getMessages()) {
        printResult(msg->result, "/get");
    }
    
    return 0;
}

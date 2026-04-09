#include "cloud/provider.hpp"
#include "cloud/transaction.hpp"
#include "network/message_result.hpp"
#include <iostream>

void printResult(const myblob::network::MessageResult& result, const std::string& path) {
    std::cout << "=== " << path << " ===" << std::endl;
    std::cout << "状态: " << static_cast<int>(result.getState()) << std::endl;
    std::cout << "成功: " << (result.success() ? "是" : "否") << std::endl;

    auto data = result.getResult();
    std::cout << "dataVector_ 大小: " << data.size() << " 字节" << std::endl;
    std::cout << "response_ length: " << result.getSize() << " 字节" << std::endl;

    if (!data.empty()) {
        std::cout << "响应内容:" << std::endl;
        std::cout.write(reinterpret_cast<const char*>(data.data()), data.size());
        std::cout << std::endl;
    } else {
        std::cout << "错误码: " << result.getFailureCode() << std::endl;
    }
    std::cout << std::endl;
}

int main() {
    std::cout << "=== MyBlob HTTP/HTTPS 测试 ===" << std::endl;
    std::cout << "测试地址: httpbin.org" << std::endl;
    std::cout << std::endl;

    auto provider = myblob::cloud::Provider::makeProvider("http://httpbin.org");
    if (!provider) {
        std::cerr << "创建 Provider 失败" << std::endl;
        return 1;
    }

    myblob::cloud::Transaction transaction(provider.get());

    constexpr size_t kBufferSize = 4096;
    uint8_t buffer1[kBufferSize];
    uint8_t buffer2[kBufferSize];
    uint8_t buffer3[kBufferSize];

    std::cout << "添加 GET /get 请求..." << std::endl;
    transaction.getObjectRequest("/get", {0, 0}, buffer1, sizeof(buffer1));

    std::cout << "添加 GET /ip 请求（返回IP）..." << std::endl;
    transaction.getObjectRequest("/ip", {0, 0}, buffer2, sizeof(buffer2));

    std::cout << "添加 GET /uuid 请求（返回UUID）..." << std::endl;
    transaction.getObjectRequest("/uuid", {0, 0}, buffer3, sizeof(buffer3));

    std::cout << "共添加了 " << transaction.getMessages().size() << " 个请求" << std::endl;
    std::cout << "开始执行..." << std::endl;
    std::cout << std::endl;

    transaction.execute();

    std::cout << "\n=== 执行结果 ===" << std::endl;
    int i = 0;
    const char* paths[] = {"/get", "/ip", "/uuid"};
    for (auto& msg : transaction.getMessages()) {
        if (i < 3) {
            printResult(msg->result, paths[i]);
        }
        ++i;
    }

    return 0;
}
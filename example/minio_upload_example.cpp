#include "cloud/provider.hpp"
#include "cloud/transaction.hpp"
#include <iostream>
#include <string>
#include <algorithm>
#include <cstdlib>

int main() {
    using namespace myblob;

    const char* env = getenv("MYBLOB_ENDPOINT");
    std::string endpoint = env ? env : "minio://192.168.17.1:9000/test1/";
    auto provider = cloud::Provider::makeProvider(endpoint, false, "minioadmin", "minioadmin", nullptr);

    if (!provider) {
        std::cerr << "创建 MinIO Provider 失败" << std::endl;
        return 1;
    }

    const std::string objectName = "myblob-minio-upload-test.txt";
    const std::string content = "hello from myblob minio upload test\n";

    std::cout << "上传到 MinIO API: http://192.168.17.1:9000/test1/" << objectName << std::endl;

    cloud::Transaction transaction(provider.get());
    uint8_t buffer[8192] = {};

    if (!transaction.putObjectRequest(objectName, content.data(), content.size(), buffer, sizeof(buffer))) {
        std::cerr << "创建 MinIO PUT 请求失败" << std::endl;
        return 1;
    }

    transaction.execute();

    for (auto& msg : transaction.getMessages()) {
        auto& result = msg->result;
        std::cout << "状态: " << static_cast<int>(result.getState()) << std::endl;
        std::cout << "成功: " << (result.success() ? "是" : "否") << std::endl;
        std::cout << "错误码: " << result.getFailureCode() << std::endl;
        std::cout << "响应码: " << result.getResponseCodeNumber() << std::endl;
        std::cout << "大小: " << result.getSize() << std::endl;
        auto data = result.getResult();
        if (!data.empty()) {
            std::cout.write(data.data(), std::min<size_t>(data.size(), 512));
            std::cout << std::endl;
        }
    }

    return 0;
}

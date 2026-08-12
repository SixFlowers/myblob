#include "cloud/provider.hpp"
#include "cloud/transaction.hpp"
#include "cloud/minio.hpp"
#include <iostream>
#include <string>
#include <vector>
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

    auto* minio = dynamic_cast<cloud::MinIO*>(provider.get());
    if (!minio) {
        std::cerr << "Provider 不是 MinIO" << std::endl;
        return 1;
    }

    constexpr size_t partSize = 6ull << 20;
    minio->setMultipartUploadSize(partSize);

    const std::string objectName = "myblob-minio-multipart-test.txt";
    std::string content((partSize << 1) + 123, 'a');
    for (size_t i = 0; i < content.size(); i++) {
        content[i] = static_cast<char>('a' + (i % 26));
    }

    std::cout << "MinIO multipart 上传: http://192.168.17.1:9000/test1/" << objectName << std::endl;
    std::cout << "分片大小: " << partSize << std::endl;
    std::cout << "总大小: " << content.size() << std::endl;

    cloud::Transaction transaction(provider.get());
    uint8_t buffer[8192] = {};
    bool callbackInvoked = false;

    auto callback = [&callbackInvoked](network::MessageResult& result) {
        callbackInvoked = true;
        std::cout << "回调状态: " << static_cast<int>(result.getState()) << std::endl;
        std::cout << "回调成功: " << (result.success() ? "是" : "否") << std::endl;
        std::cout << "回调错误码: " << result.getFailureCode() << std::endl;
        std::cout << "回调响应码: " << result.getResponseCodeNumber() << std::endl;
        std::cout << "回调大小: " << result.getSize() << std::endl;
        if (auto origin = result.getOriginError()) {
            std::cout << "来源状态: " << static_cast<int>(origin->getState()) << std::endl;
            std::cout << "来源成功: " << (origin->success() ? "是" : "否") << std::endl;
            std::cout << "来源错误码: " << origin->getFailureCode() << std::endl;
            std::cout << "来源响应码: " << origin->getResponseCodeNumber() << std::endl;
            std::cout << "来源大小: " << origin->getSize() << std::endl;
            auto originData = origin->getResult();
            if (!originData.empty()) {
                std::cout.write(originData.data(), std::min<size_t>(originData.size(), 2048));
                std::cout << std::endl;
            }
        }
    };

    if (!transaction.putObjectRequest(callback, objectName, content.data(), content.size(), buffer, sizeof(buffer))) {
        std::cerr << "创建 MinIO multipart PUT 请求失败" << std::endl;
        return 1;
    }

    transaction.execute();

    std::cout << "回调是否执行: " << (callbackInvoked ? "是" : "否") << std::endl;

    for (auto& msg : transaction.getMessages()) {
        auto& result = msg->result;
        std::cout << "主消息状态: " << static_cast<int>(result.getState()) << std::endl;
        std::cout << "主消息成功: " << (result.success() ? "是" : "否") << std::endl;
        std::cout << "主消息错误码: " << result.getFailureCode() << std::endl;
        std::cout << "主消息响应码: " << result.getResponseCodeNumber() << std::endl;
        std::cout << "主消息大小: " << result.getSize() << std::endl;
        auto data = result.getResult();
        if (!data.empty()) {
            std::cout.write(data.data(), std::min<size_t>(data.size(), 512));
            std::cout << std::endl;
        }
    }

    return callbackInvoked ? 0 : 2;
}

#include "cloud/provider.hpp"
#include "cloud/transaction.hpp"
#include <functional>

void onDownloadComplete(network::MessageResult& result) {
    if (result.success()) {
        std::cout << "下载成功! 大小: " << result.getSize() << std::endl;
    } else {
        std::cout << "下载失败! 状态: " << static_cast<int>(result.getState()) << std::endl;
    }
}

int main() {
    auto provider = Provider::makeProvider("http://example.com/file.txt");
    if (!provider) return 1;
    
    Transaction transaction(provider.get());
    
    transaction.getObjectRequest(onDownloadComplete, "/data/file.txt");
    
    std::cout << "请求已添加，异步处理中..." << std::endl;
    
    return 0;
}
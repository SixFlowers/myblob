#include "cloud/provider.hpp"
#include "cloud/transaction.hpp"
#include <iostream>

int main() {
    auto provider = myblob::cloud::Provider::makeProvider("http://httpbin.org");
    if (!provider) return 1;
    
    myblob::cloud::Transaction transaction(provider.get());
    
    uint8_t buffer[1024];
    if (transaction.getObjectRequest("/get", {0, 0}, buffer, sizeof(buffer))) {
        std::cout << "请求已添加到事务" << std::endl;
        
        for (auto& msg : transaction.getMessages()) {
            auto& result = msg->result;
            
            std::cout << "状态: " << static_cast<int>(result.getState()) << std::endl;
            std::cout << "成功: " << (result.success() ? "是" : "否") << std::endl;
            
            if (result.success()) {
                std::cout << "大小: " << result.getSize() << std::endl;
            }
        }
    }
    
    return 0;
}

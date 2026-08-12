#include "cloud/provider.hpp"
#include "cloud/transaction.hpp"
#include <iostream>
#include <cstdlib>

int main() {
    const char* env = getenv("MYBLOB_ENDPOINT");
    std::string endpoint = env ? env : "http://httpbin.org";
    auto provider = myblob::cloud::Provider::makeProvider(endpoint);
    if (!provider) return 1;
    
    myblob::cloud::Transaction transaction(provider.get());
    
    uint8_t buffer[1024];
    if (transaction.getObjectRequest("/get", {0, 0}, buffer, sizeof(buffer))) {
        std::cout << "请求已添加到事务" << std::endl;
        
        transaction.execute();
        
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

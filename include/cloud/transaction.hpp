#pragma once
#include "cloud_service.hpp"
#include "provider.hpp"
#include "../network/original_message.hpp"
#include "../network/connection_manager.hpp"
#include "../network/socket.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <thread>

namespace myblob::cloud {

// 事务类用于处理云存储操作
class Transaction {
public:
    Transaction();
    explicit Transaction(Provider* provider);
    
    void setProvider(Provider* provider);
    
    // 添加GET请求
    bool getObjectRequest(
        const std::string& remotePath,
        std::pair<uint64_t, uint64_t> range,
        uint8_t* result,
        uint64_t capacity
    );
    
    // 执行所有请求
    void execute();
    
    // 异步执行
    void executeAsync();
    
    // 异步执行带回调（模板函数在头文件中实现）
    template<typename Callback>
    void executeAsync(Callback&& callback) {
        std::thread([this, callback]() {
            execute();
            callback();
        }).detach();
    }
    
    // 获取消息列表
    const std::vector<std::unique_ptr<network::OriginalMessage>>& getMessages() const {
        return messages_;
    }

private:
    Provider* provider_;
    std::vector<std::unique_ptr<network::OriginalMessage>> messages_;
    std::unique_ptr<network::ConnectionManager> connMgr_;
    std::vector<std::unique_ptr<network::Socket::Request>> requestHolder_;
};

} // namespace myblob::cloud

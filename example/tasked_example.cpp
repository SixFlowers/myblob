#include "cloud/provider.hpp"
#include "network/tasked_send_receiver.hpp"
#include "network/config.hpp"
#include "utils/timer.hpp"
#include <iostream>
#include <vector>
#include <thread>

using namespace myblob;

// ============================================================================
// 示例 1：基本使用
// ============================================================================
void basicExample() {
    std::cout << "=== 示例 1：基本使用 ===" << std::endl;
    
    // 创建任务调度器组
    network::TaskedSendReceiverGroup group(
        64 * 1024,  // 64KB 块大小
        1024,       // 提交队列大小
        1024        // 复用池大小
    );
    
    // 设置并发请求数
    group.setConcurrentRequests(20);
    
    std::cout << "并发请求数: " << group.getConcurrentRequests() << std::endl;
    
    // 获取句柄
    auto handle = group.getHandle();
    
    if (handle.has()) {
        std::cout << "成功获取调度器句柄" << std::endl;
    }
    
    std::cout << std::endl;
}

// ============================================================================
// 示例 2：使用 Config 配置
// ============================================================================
void configExample() {
    std::cout << "=== 示例 2：使用 Config 配置 ===" << std::endl;
    
    // 创建配置
    network::Config config;
    config.coreThroughput = 8000;   // 8 Gbps 每核心
    config.coreConcurrency = 20;    // 20 个并发请求
    config.network = 40000;         // 40 Gbps 网络
    
    std::cout << "网络带宽: " << config.bandwidth() << " Mbit/s" << std::endl;
    std::cout << "每核心请求数: " << config.coreRequests() << std::endl;
    std::cout << "需要线程数: " << config.retrievers() << std::endl;
    std::cout << "总并发请求数: " << config.totalRequests() << std::endl;
    
    // 创建调度器组并应用配置
    network::TaskedSendReceiverGroup group;
    group.setConfig(config);
    
    std::cout << std::endl;
}

// ============================================================================
// 示例 3：使用 Timer 计时
// ============================================================================
void timerExample() {
    std::cout << "=== 示例 3：使用 Timer 计时 ===" << std::endl;
    
    utils::Timer timer(&std::cout);
    
    // 使用 TimerGuard 自动计时
    {
        utils::Timer::TimerGuard guard(utils::Timer::Steps::Download, &timer);
        
        // 模拟下载操作
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "计时完成" << std::endl;
    std::cout << std::endl;
}

// ============================================================================
// 示例 4：使用 RingBuffer
// ============================================================================
void ringBufferExample() {
    std::cout << "=== 示例 4：使用 RingBuffer ===" << std::endl;
    
    // 创建环形队列
    utils::RingBuffer<int> buffer(10);
    
    // 插入元素
    for (int i = 0; i < 5; i++) {
        (void)buffer.insert(i);
        std::cout << "插入: " << i << std::endl;
    }
    
    // 消费元素
    while (!buffer.empty()) {
        auto val = buffer.consume();
        if (val.has_value()) {
            std::cout << "消费: " << val.value() << std::endl;
        }
    }
    
    std::cout << std::endl;
}

// ============================================================================
// 示例 5：多线程处理
// ============================================================================
void multiThreadExample() {
    std::cout << "=== 示例 5：多线程处理 ===" << std::endl;
    
    // 创建配置
    network::Config config;
    config.coreThroughput = 8000;
    config.coreConcurrency = 10;
    config.network = 16000;  // 16 Gbps，需要 2 个线程
    
    // 创建调度器组
    network::TaskedSendReceiverGroup group(
        64 * 1024,
        config.retrievers() * 1024
    );
    group.setConfig(config);
    
    std::cout << "启动 " << config.retrievers() << " 个处理线程" << std::endl;
    
    // 启动多个处理线程（只处理一轮，不持续运行）
    std::vector<std::thread> threads;
    for (uint64_t i = 0; i < config.retrievers(); i++) {
        threads.emplace_back([&group]() {
            auto handle = group.getHandle();
            handle.process(true);  // 只处理一轮
        });
    }
    
    // 等待线程
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "多线程处理完成" << std::endl;
    std::cout << std::endl;
}

// ============================================================================
// 主函数
// ============================================================================
int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "   MyBlob 第七版：任务调度器示例" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    basicExample();
    configExample();
    timerExample();
    ringBufferExample();
    multiThreadExample();
    
    std::cout << "所有示例完成！" << std::endl;
    
    return 0;
}
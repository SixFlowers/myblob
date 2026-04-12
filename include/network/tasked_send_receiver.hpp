#pragma once
#include "network/config.hpp"
#include "network/connection_manager.hpp"
#include "network/message_state.hpp"
#include "network/original_message.hpp"
#include "network/socket.hpp"
#include "utils/ring_buffer.hpp"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace myblob::utils {
template <typename T>
class DataVector;
struct TimingHelper;
}  // namespace myblob::utils

namespace myblob::network {

class TaskedSendReceiver;
class TaskedSendReceiverHandle;
class Cache;
class MessageTask;

//多线程调度器组
class TaskedSendReceiverGroup {
  //全局提交队列
  utils::RingBuffer<OriginalMessage*> _submissions;
  //内存复用池
  utils::RingBuffer<utils::DataVector<uint8_t>*> _reuse;
  //调度器列表
  std::vector<std::unique_ptr<TaskedSendReceiver>> _sendReceivers;
  //调整大小的互斥锁
  std::mutex _resizeMutex;
  //调度器缓存
  utils::RingBuffer<TaskedSendReceiver*> _sendReceiverCache;
  //每个线程的提交队列大小
  static constexpr uint64_t submissionPerCore = 1 << 10;
  //接收块大小
  uint64_t _chunkSize;
  //调度器最大并发请求数
  unsigned _concurrentRequests;
  //TCP设置
  std::unique_ptr<TCPSettings> _tcpSettings;
  std::condition_variable _cv;
  //条件变量锁
  std::mutex _mutex;
public:
  explicit TaskedSendReceiverGroup(
    unsigned chunkSize = 64u*1024,
    uint64_t submissions = std::thread::hardware_concurrency()*submissionPerCore,
    uint64_t reuse = 0
  );
  //析构函数
  ~TaskedSendReceiverGroup();
  //添加消息到提交队列
  [[nodiscard]] bool send(OriginalMessage* msg);
  //批量添加消息到提交队列
  [[nodiscard]] bool send(const std::vector<OriginalMessage*>& msgs);
  //获取调度器句柄
  [[nodiscard]] TaskedSendReceiverHandle getHandle(); 
  //处理队列中的请求
  void process(bool oneQueueInvocation = true);
  //配置方法
  //更新并发请求数
  void setConfig(const Config& config){
    if(_concurrentRequests != config.totalRequests()){
      _concurrentRequests = config.totalRequests();
    }
  }
  //直接设置并发请求数
  void setConcurrentRequests(unsigned concurrentRequests){
    if(_concurrentRequests != concurrentRequests){
      _concurrentRequests = concurrentRequests;
    }
  }
  //获取并发请求数
  unsigned getConcurrentRequests() const {
    return _concurrentRequests;
  }
  friend class TaskedSendReceiver;
  friend class TaskedSendReceiverHandle;
};

class TaskedSendReceiver{
private:
  //所属组
  TaskedSendReceiverGroup& _group;
  //本地提交队列
  std::queue<OriginalMessage*> _submissions;
  //下一个调度器(用于链表)
  std::atomic<TaskedSendReceiver*> _next;
  //连接管理器
  std::unique_ptr<ConnectionManager> _connectionManager;
  //当前任务列表
  std::vector<std::unique_ptr<MessageTask>> _messageTasks;
  //计时信息
  std::vector<utils::TimingHelper>* _timings;
  //停止标记
  std::atomic<bool> _stopDeamon;
public:
  [[nodiscard]] const TaskedSendReceiverGroup* getGroup() const{
    return &_group;
  }
  //域名特定缓存
  void addCache(const std::string& hostname, std::unique_ptr<Cache> cache);
  //同步处理本地提交
  inline void processSync(bool oneQueueInvocation = true){
    sendReceive(true, oneQueueInvocation);
  }
  //设置计时信息
  void setTimings(std::vector<utils::TimingHelper>* timings){
    _timings = timings;
  }
  void reuse(std::unique_ptr<utils::DataVector<uint8_t>> message);
  std::unique_ptr<utils::DataVector<uint8_t>> getReused();
private:
  TaskedSendReceiver() = delete;
  TaskedSendReceiver(TaskedSendReceiver& other) = delete;
  TaskedSendReceiver& operator=(TaskedSendReceiver& other) = delete;
  explicit TaskedSendReceiver(TaskedSendReceiverGroup& group);
  void sendSync(OriginalMessage* msg);
  //停止调度器
  void stop(){
    _stopDeamon = true;
  }
  //发送接收循环
  void sendReceive(bool local = false, bool oneQueueInvocation = true);
  //提交请求
  [[nodiscard]] int32_t submitRequests();
  //重置调度器
  void reset();
  // 友元类声明
  friend class TaskedSendReceiverGroup;
  friend class TaskedSendReceiverHandle;
};

class TaskedSendReceiverHandle{
private:
  TaskedSendReceiverGroup* _group;
  TaskedSendReceiver* _sendReceiver;
  TaskedSendReceiverHandle() = delete;
  explicit TaskedSendReceiverHandle(TaskedSendReceiverGroup* group, TaskedSendReceiver* sendReceiver);
  TaskedSendReceiverHandle(TaskedSendReceiverHandle& other) = delete;
  TaskedSendReceiverHandle& operator=(TaskedSendReceiverHandle& other) = delete;
  //发送接收
  bool sendReceive(bool local, bool oneQueueInvocation = true);
public:
  TaskedSendReceiverHandle(TaskedSendReceiverHandle&& other) noexcept;
  TaskedSendReceiverHandle& operator=(TaskedSendReceiverHandle&& other) noexcept;
  ~TaskedSendReceiverHandle();
  //处理组提交(异步)
  inline bool process(bool oneQueueInvocation = true){
    return sendReceive(false, oneQueueInvocation);
  }
  //处理本地提交(同步)
  inline bool processSync(bool oneQueueInvocation = true){
    return sendReceive(true, oneQueueInvocation);
  }
  bool sendSync(OriginalMessage* msg);
  //停止句柄线程
  void stop();
  TaskedSendReceiver* get(){
    return _sendReceiver;
  }
  TaskedSendReceiverGroup* getGroup(){
    return _group;
  }
  inline bool has() const{
    return _sendReceiver != nullptr;
  }
  friend class TaskedSendReceiverGroup;
};

struct MessageTask{
  OriginalMessage* originalMessage;
  uint64_t receiveBufferOffset;
  static std::unique_ptr<MessageTask> buildMessageTask(
    OriginalMessage* msg,
    const TCPSettings& settings,
    uint64_t chunkSize
  );
  MessageState execute(ConnectionManager& connMgr);
};

}

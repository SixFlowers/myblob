#pragma once
#include "network/message_task.hpp"
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

//多线程调度器组：管理一组 TaskedSendReceiver（每个线程一个）
//职责：
//1. 提供全局提交队列（_submissions），多线程安全地入队请求
//2. 管理调度器池（_sendReceivers + _sendReceiverCache），按需创建/复用
//3. 管理内存复用池（_reuse），减少 DataVector 的 malloc/free
//4. 统一配置（chunkSize、concurrentRequests、tcpSettings）
class TaskedSendReceiverGroup {
  utils::RingBuffer<OriginalMessage*> _submissions;      //全局无锁提交队列，多线程通过 send() 入队
  utils::RingBuffer<utils::DataVector<uint8_t>*> _reuse;  //内存复用池：完成的 DataVector 不 free，放这里给新请求复用
  std::vector<std::unique_ptr<TaskedSendReceiver>> _sendReceivers;  //所有已创建的调度器（每个绑定一个线程）
  std::mutex _resizeMutex;                                 //保护 _sendReceivers 向量扩缩容
  utils::RingBuffer<TaskedSendReceiver*> _sendReceiverCache;  //空闲调度器缓存，Handle 归还时放入，getHandle() 优先取
  static constexpr uint64_t submissionPerCore = 1 << 10;  //每核心默认提交队列大小 = 1024
  uint64_t _chunkSize;                                     //每次 IO 的最大字节数（默认 64KB）
  unsigned _concurrentRequests;                            //每个调度器最大并发请求数
  std::unique_ptr<TCPSettings> _tcpSettings;               //TCP 配置（超时、keepalive 等），所有调度器共享
  std::condition_variable _cv;                             //用于等待请求完成（如同步发送时）
  std::mutex _mutex;                                       //_cv 的配套锁
public:
  //构造函数：chunkSize=64KB, submissions=核心数*1024, reuse=0(默认与submissions相同)
  explicit TaskedSendReceiverGroup(
    unsigned chunkSize = 64u*1024,
    uint64_t submissions = std::thread::hardware_concurrency()*submissionPerCore,
    uint64_t reuse = 0
  );
  ~TaskedSendReceiverGroup();
  //将单个请求入队到全局提交队列（多线程安全）
  [[nodiscard]] bool send(OriginalMessage* msg);
  //批量入队（多线程安全）
  [[nodiscard]] bool send(const std::vector<OriginalMessage*>& msgs);
  //获取一个调度器句柄（优先从缓存取，没有则新建）
  //Handle 是 RAII 包装，析构时自动归还调度器到缓存
  [[nodiscard]] TaskedSendReceiverHandle getHandle(); 
  //便捷方法：获取 Handle + 处理一轮请求
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

//调度器：绑定一个线程，持有 ConnectionManager 和消息任务列表
//核心方法 sendReceive() 是事件循环，驱动所有 MessageTask 的状态机
//每个 TaskedSendReceiver 独立运行在自己的线程上，互不干扰
class TaskedSendReceiver{
private:
  TaskedSendReceiverGroup& _group;                          //所属的 Group（获取共享配置和提交队列）
  std::queue<OriginalMessage*> _submissions;                //本地提交队列（sendSync() 入队，优先处理）
  std::atomic<TaskedSendReceiver*> _next;                   //链表指针（用于无锁队列，当前未使用）
  std::unique_ptr<ConnectionManager> _connectionManager;     //连接管理器（连接池 + Socket 实例）
  std::vector<std::unique_ptr<MessageTask>> _messageTasks;  //当前正在处理的消息任务列表
  std::vector<utils::TimingHelper>* _timings;               //性能计时（可选，由上层设置）
  std::atomic<bool> _stopDeamon;                            //停止标志，设为 true 则事件循环退出
public:
  [[nodiscard]] const TaskedSendReceiverGroup* getGroup() const{
    return &_group;
  }
  // 域名特定缓存：为指定 hostname 启用吞吐量缓存
  void addCache(const std::string& hostname, std::unique_ptr<Cache> cache);
  //同步处理本地提交队列（local=true 的 sendReceive）
  inline void processSync(bool oneQueueInvocation = true){
    sendReceive(true, oneQueueInvocation);
  }
  //设置性能计时数组
  void setTimings(std::vector<utils::TimingHelper>* timings){
    _timings = timings;
  }
  //将 DataVector 放入复用池（不 free，留给后续请求复用）
  void reuse(std::unique_ptr<utils::DataVector<uint8_t>> message);
  //从复用池取一个 DataVector（避免 malloc）
  std::unique_ptr<utils::DataVector<uint8_t>> getReused();
private:
  TaskedSendReceiver() = delete;
  TaskedSendReceiver(TaskedSendReceiver& other) = delete;
  TaskedSendReceiver& operator=(TaskedSendReceiver& other) = delete;
  explicit TaskedSendReceiver(TaskedSendReceiverGroup& group);  //只能由 Group 创建
  void sendSync(OriginalMessage* msg);  //将请求放入本地提交队列
  void stop(){                          //设置停止标志，事件循环将退出
    _stopDeamon = true;
  }
  //★核心方法★：事件循环，驱动所有 MessageTask 的状态机
  //local=true: 从本地 _submissions 取请求（同步模式）
  //local=false: 从全局 _group._submissions 取请求（异步模式）
  //oneQueueInvocation=true: 队列空且无待完成 IO 时退出循环
  //oneQueueInvocation=false: 持续运行直到 stop()
  void sendReceive(bool local = false, bool oneQueueInvocation = true);
  [[nodiscard]] int32_t submitRequests();  //提交所有挂起的 IO 请求到内核
  void reset();                           //清空本地队列和任务列表，准备复用
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

}

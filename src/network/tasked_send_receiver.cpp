#include "network/tasked_send_receiver.hpp"
#include "network/message_result.hpp"
#include "network/original_message.hpp"
// #include "network/tls_context.hpp"
#include "utils/data_vector.hpp"
#include "utils/timer.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>

namespace myblob::network {

TaskedSendReceiverGroup::TaskedSendReceiverGroup(
    unsigned chunkSize,
    uint64_t submissions,
    uint64_t reuse
) : _submissions(submissions)
  , _reuse(!reuse ? submissions : reuse)
  , _sendReceivers()
  , _resizeMutex()
  , _sendReceiverCache(submissions)
  , _chunkSize(chunkSize)
  , _concurrentRequests(Config::defaultCoreConcurrency)
  , _tcpSettings(std::make_unique<TCPSettings>())
  , _cv()
  , _mutex()
{
    // TLSContext::initOpenSSL();
}

//释放复用池的内存
TaskedSendReceiverGroup::~TaskedSendReceiverGroup(){
  while(auto val = _reuse.consume()){
    if(val.has_value()){
      delete val.value();
    }
  }
}

bool TaskedSendReceiverGroup::send(OriginalMessage* msg){
  return (_submissions.insert(msg) != ~0ull); 
}

bool TaskedSendReceiverGroup::send(const std::vector<OriginalMessage*>& msgs){
  return (_submissions.insertAll(msgs) != ~0ull);
}

TaskedSendReceiverHandle TaskedSendReceiverGroup::getHandle(){
  auto result = _sendReceiverCache.consume();
  if(result.has_value()){
    return TaskedSendReceiverHandle(this, result.value());
  }
  std::lock_guard<std::mutex> lg(_resizeMutex);
  auto& ref = _sendReceivers.emplace_back(std::unique_ptr<TaskedSendReceiver>(new TaskedSendReceiver(*this)));
  return TaskedSendReceiverHandle(this, ref.get());
}

void TaskedSendReceiverGroup::process(bool oneQueueInvocation) {
  auto handle = getHandle();
  handle.process(oneQueueInvocation);
}

TaskedSendReceiverHandle::TaskedSendReceiverHandle(
    TaskedSendReceiverGroup* group, 
    TaskedSendReceiver* sendReceiver
) : _group(group)
  , _sendReceiver(sendReceiver)
{
}

TaskedSendReceiverHandle::TaskedSendReceiverHandle(
    TaskedSendReceiverHandle&& other
) noexcept {
    _group = other._group;
    _sendReceiver = other._sendReceiver;
    other._sendReceiver = nullptr;
}

TaskedSendReceiverHandle& TaskedSendReceiverHandle::operator=(
    TaskedSendReceiverHandle&& other
) noexcept {
    if (this == &other) return *this;
    // Release current resource
    if (_sendReceiver && _group) {
        _sendReceiver->reset();
        (void)_group->_sendReceiverCache.insert(_sendReceiver);
    }
    // Transfer from other
    _group = other._group;
    _sendReceiver = other._sendReceiver;
    other._sendReceiver = nullptr;
    return *this;
}

TaskedSendReceiverHandle::~TaskedSendReceiverHandle() {
  if(!_sendReceiver || !_sendReceiver->getGroup()){
    return;
  }
  _sendReceiver->reset();
  auto ptr = _sendReceiver;
  if(_group->_sendReceiverCache.insert(ptr) == ~0ull){
    //缓存满了，删除
    std::lock_guard<std::mutex> lg(_group->_resizeMutex);
    _group->_sendReceivers.erase(
      std::remove_if(
        _group->_sendReceivers.begin(),
        _group->_sendReceivers.end(),
        [this](auto& val){
          return val.get() == _sendReceiver;
        }
      ),
      _group->_sendReceivers.end()
    );
  }
  _sendReceiver = nullptr;
}

bool TaskedSendReceiverHandle::sendSync(OriginalMessage* msg){
  if(!_sendReceiver){
    return false;
  }
  _sendReceiver->sendSync(msg);
  return true;
}

void TaskedSendReceiverHandle::stop(){
  if(!_sendReceiver){
    return;
  }
  _sendReceiver->stop();
}

bool TaskedSendReceiverHandle::sendReceive(bool local, bool oneQueueInvocation){
  if(!_sendReceiver){
    return false;
  }
  _sendReceiver->sendReceive(local, oneQueueInvocation);
  return true;
}

TaskedSendReceiver::TaskedSendReceiver(TaskedSendReceiverGroup& group)
    : _group(group)
    , _submissions()
    , _next(nullptr)
    , _connectionManager(std::make_unique<ConnectionManager>(group._concurrentRequests << 2))
    , _messageTasks()
    , _timings(nullptr)
    , _stopDeamon(false)
{
}

void TaskedSendReceiver::sendSync(OriginalMessage* msg){
  _submissions.emplace(msg);
}

void TaskedSendReceiver::reuse(std::unique_ptr<utils::DataVector<uint8_t>> message){
  (void)_group._reuse.insert(message.release());
}

std::unique_ptr<utils::DataVector<uint8_t>> TaskedSendReceiver::getReused(){
  auto mem = _group._reuse.consume();
  if(mem.has_value()){
    return std::unique_ptr<utils::DataVector<uint8_t>>(mem.value());
  }
  return nullptr;
}

void TaskedSendReceiver::addCache(const std::string& hostname,
                                   std::unique_ptr<Cache> cache){
  if (!cache || !_connectionManager) return;
  // Enable throughput-aware caching in the connection manager.
  // The cache tracks per-socket throughput percentiles and influences
  // connection pool eviction: high-throughput connections get higher
  // cache priority and survive longer in the pool.
  if (!_connectionManager->hasCache()) {
    _connectionManager->enableThroughputCache();
    std::cout << "[Cache] Throughput cache enabled for " << hostname << std::endl;
  }
  // Per-hostname cache entries will be populated as connections are
  // returned to the pool via returnConnection().
  (void)cache;  // cache ownership transferred elsewhere or replaced by ThroughputCache
}

void TaskedSendReceiver::sendReceive(bool local, bool oneQueueInvocation){//处理任务
  _stopDeamon = false;
  std::exception_ptr firstException = nullptr;
  //当前在处理的请求数
  auto count = 0u;
  
  auto emplaceNewRequest = [&](){
    while(auto val = _group._submissions.consume()){
      if(!val.has_value()){
        break;
      }
      auto original = val.value();
      auto messageTask = MessageTask::buildMessageTask(original, *_group._tcpSettings, _group._chunkSize);
      if (!messageTask) {
        original->result.setState(MessageState::Aborted);
        continue;
      }
      //尝试复用内存
      if(!original->result.getDataVector().capacity()){
        auto mem = _group._reuse.consume();
        if(mem.has_value()){
          original->setResultVector(mem.value());
        }
      }
      if(_timings){
        (*_timings)[original->traceId].start = std::chrono::steady_clock::now();
      }
      try{
        auto status = messageTask->execute(*_connectionManager);
        if(status == MessageState::Aborted){
          if(messageTask->originalMessage->requiresFinish()){
            messageTask->originalMessage->finish();
          }
          continue;
        }
        if(status == MessageState::Finished){
          if(_timings){
            (*_timings)[messageTask->originalMessage->traceId].size = messageTask->originalMessage->result.getSize();
            (*_timings)[messageTask->originalMessage->traceId].finish = std::chrono::steady_clock::now();
          }
          if(messageTask->originalMessage->requiresFinish()){
            messageTask->originalMessage->finish();
          }
          continue;
        }
      }catch(const std::exception& e){
        std::cerr << "[ERROR] TaskedSendReceiver: " << e.what() << std::endl;
        if(!firstException){
          firstException = std::current_exception();
        }
        return;
      }
      _messageTasks.emplace_back(std::move(messageTask));
      if(_messageTasks.size() >= _group._concurrentRequests){
        break;
      }
    }
  };
  
  auto emplaceLocalRequest = [&](){
    while(!_submissions.empty()){
      auto original = _submissions.front();
      _submissions.pop();
      auto messageTask = MessageTask::buildMessageTask(original, *_group._tcpSettings, _group._chunkSize);
      assert(messageTask.get());
      if(!original->result.getDataVector().capacity()){
        auto mem = _group._reuse.consume();
        if(mem.has_value()){
          original->setResultVector(mem.value());
        }
      }
      if(_timings){
        (*_timings)[original->traceId].start = std::chrono::steady_clock::now();
      }
      try{
        auto status = messageTask->execute(*_connectionManager);
        if(status == MessageState::Aborted){
          if(messageTask->originalMessage->requiresFinish()){
            messageTask->originalMessage->finish();
          }
          continue;
        }
        if(status == MessageState::Finished){
          if(_timings){
            (*_timings)[messageTask->originalMessage->traceId].size = messageTask->originalMessage->result.getSize();
            (*_timings)[messageTask->originalMessage->traceId].finish = std::chrono::steady_clock::now();
          }
          if(messageTask->originalMessage->requiresFinish()){
            messageTask->originalMessage->finish();
          }
          continue;
        }
      }catch(const std::exception& e){
        std::cerr << "[ERROR] TaskedSendReceiver: " << e.what() << std::endl;
        if(!firstException){
          firstException = std::current_exception();
        }
        return;
      }
      _messageTasks.emplace_back(std::move(messageTask));
      if(_messageTasks.size() >= _group._concurrentRequests){
        break;
      }
    }
  };
  
  while(!_stopDeamon || count){
    if(count > 0){
      Socket::Request* req = nullptr;
      try{
        req = _connectionManager->getSocket().complete();
      }catch(const std::exception& e){
        continue;
      }
      if(!req || !req->userData){
        continue;
      }
      count--;  // only decrement on valid completion
      auto task = reinterpret_cast<MessageTask*>(req->userData);
      try{
        auto status = task->execute(*_connectionManager);
        if(status == MessageState::Finished || status == MessageState::Aborted){
          for(auto it = _messageTasks.begin(); it != _messageTasks.end(); it++){
            if(it->get() == task){
              if(_timings){
                (*_timings)[task->originalMessage->traceId].size = 
                  (status == MessageState::Aborted) ? 0 : task->originalMessage->result.getSize();
                (*_timings)[task->originalMessage->traceId].finish = std::chrono::steady_clock::now();
              }
              if(task->originalMessage->requiresFinish()){
                task->originalMessage->finish();
              }
              // 回收缓冲区到复用池（避免频繁 malloc/free）
              {
                auto dv = task->originalMessage->result.moveDataVector();
                if (dv) {
                  reuse(std::move(dv));
                }
              }
              _messageTasks.erase(it);
              _group._cv.notify_all();
              break;
            }
          }
        }else if(_timings && status == MessageState::Receiving && !task->receiveBufferOffset){
          (*_timings)[task->originalMessage->traceId].receive = std::chrono::steady_clock::now();
        }
      }catch(const std::exception& e){
        std::cerr << "[ERROR] TaskedSendReceiver: " << e.what() << std::endl;
        if(!firstException){
          firstException = std::current_exception();
        }
      }
    }
    if(!_stopDeamon && _messageTasks.size() < _group._concurrentRequests && !firstException){
      local ? emplaceLocalRequest() : emplaceNewRequest();
    }
    auto cnt = _connectionManager->getSocket().submit();
    if(cnt < 0){
      throw std::runtime_error("socket submit error:" + std::to_string(-cnt));
    }
    count += static_cast<unsigned>(cnt);
    //检查是否应该停止
    auto empty = local ? _submissions.empty() : _group._submissions.empty();
    if(oneQueueInvocation && (empty || firstException) && !count){
      _stopDeamon = true;
    }
  }
  if(oneQueueInvocation){
    _stopDeamon = false;
  }
  if(firstException){
    _connectionManager.reset();
    std::rethrow_exception(firstException);
  }
}

int32_t TaskedSendReceiver::submitRequests() {
    return _connectionManager->getSocket().submit();
}

void TaskedSendReceiver::reset() {
  while(!_submissions.empty()){
    _submissions.pop();
  }
  _messageTasks.clear();
  _stopDeamon = false;
}

}

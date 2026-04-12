#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <cstdint>
#include <vector>

//无锁环形队列
namespace myblob::utils {
template <typename T>
class RingBuffer{
public:
  class SpinMutex{
    std::atomic<bool> bit;
  public:
    constexpr SpinMutex() : bit(false){}
    void lock(){
      auto expected = false;
      while(!bit.compare_exchange_weak(expected,true,std::memory_order_acq_rel)){
        expected = false;
      }
    }
    void unlock(){
      bit.store(false,std::memory_order_release);
    }
  };
private:
//缓冲区
  std::unique_ptr<T[]> _buffer;
  //缓冲区大小
  uint64_t _size;
  struct{
    //待插入位置
    std::atomic<uint64_t> pending;
    //已提交位置
    std::atomic<uint64_t> commited;
    //插入锁
    SpinMutex mutex;
  } _insert;
  struct{
    //待消费位置
    std::atomic<uint64_t> pending;
    //已消费位置
    std::atomic<uint64_t> commited;
    //消费锁
    SpinMutex mutex;
  } _seen;
public:
  explicit RingBuffer(uint64_t size): _buffer(std::make_unique<T[]>(size)),_size(size){
    _insert.pending.store(0,std::memory_order_relaxed);
    _insert.commited.store(0,std::memory_order_relaxed);
    _seen.pending.store(0,std::memory_order_relaxed);
    _seen.commited.store(0,std::memory_order_relaxed);
  }
  
  template <bool wait = false>
  [[nodiscard]] uint64_t insert(T tuple){
    while(true){
      std::unique_lock<SpinMutex> lock(_insert.mutex);
      //获取已消费位置
      auto seenHead = _seen.commited.load(std::memory_order_acquire);
      //获取当前插入位置
      auto curInsert = _insert.pending.load(std::memory_order_acquire);
      //检查是否有空间
      if(curInsert - seenHead < _size){
        _insert.pending.fetch_add(1,std::memory_order_release);
        _buffer[curInsert % _size] = tuple;//环形缓冲区思想
        _insert.commited.fetch_add(1,std::memory_order_release);
        return curInsert;
      }else if(!wait){
        return ~0ull;
      }
    }
  }
  
  template<bool wait = false>
  [[nodiscard]] uint64_t insertAll(const std::vector<T>& tuples){
    while(true){
      std::unique_lock<SpinMutex> lock(_insert.mutex);
      auto seenHead = _seen.commited.load();
      auto curInsert = _insert.pending.load();
      if(_size >= tuples.size() && curInsert - seenHead < _size + 1 - tuples.size()){
        _insert.pending.fetch_add(tuples.size(),std::memory_order_release);
        auto off = 0u;
        for(const auto& tuple : tuples){
          _buffer[(curInsert + off) % _size] = tuple;
          off++;
        }
        _insert.commited.fetch_add(tuples.size(),std::memory_order_release);
        return curInsert;
      }else if(!wait){
        return ~0ull;
      }
    }
  }
  
  template <bool wait = false>
  [[nodiscard]] std::optional<T> consume(){
    while(true){
      std::unique_lock<SpinMutex> lock(_seen.mutex);
      auto curInsert = _insert.commited.load(std::memory_order_acquire);
      auto curSeen = _seen.pending.load(std::memory_order_acquire);
      if(curInsert > curSeen){
        _seen.pending.fetch_add(1,std::memory_order_release);
        auto val = _buffer[curSeen % _size];
        _seen.commited.fetch_add(1,std::memory_order_release);
        return val;
      }else if(!wait){
        return std::nullopt;
      }
    }
  }
  
  [[nodiscard]] bool empty() const{
    return !(_insert.commited.load(std::memory_order_acquire) - _seen.commited.load(std::memory_order_acquire));
  }
};
}

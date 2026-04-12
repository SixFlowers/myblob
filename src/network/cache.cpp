#include "network/cache.hpp"
#include <iostream>
#include <unistd.h>

namespace myblob::network {
std::unique_ptr<SocketEntry> Cache::resolve(
    const std::string& hostname, 
    unsigned port, 
    bool tls
) {
  std::string key = hostname+":"+std::to_string(port);
  auto range = _cache.equal_range(key);
  for(auto it = range.first;it != range.second;++it){
    if(it->second->fd >= 0){
      std::cout << "[Cache] Hit: " << key << std::endl;
      auto fifoIt = _fifo.find(it->second->timestamp);
      if(fifoIt != _fifo.end()){
        _fifo.erase(fifoIt);
      }
      auto entry = std::move(it->second);
      _cache.erase(it);
      return entry;
    }
  }
  //缓存未命中，创建新的SocketEntry
  std::cout << "[Cache] Miss: " << key << std::endl;
  auto entry = std::make_unique<SocketEntry>(hostname,port);
  return entry;
}
void Cache::stopSocket(
    std::unique_ptr<SocketEntry> socketEntry,
    uint64_t bytes,
    unsigned cachedEntries,
    bool reuseSocket
) {
  //不复用，直接关闭
  if(!reuseSocket || !socketEntry || socketEntry->fd<0){
    shutdownSocket(std::move(socketEntry),cachedEntries);
    return;
  }
  if(cachedEntries>=100){
    if(!_fifo.empty()){
      auto oldest = _fifo.begin()->second;
      std::string key = oldest->hostname + ":" + std::to_string(oldest->port);
      auto range = _cache.equal_range(key);
      for(auto it = range.first;it != range.second;++it){
        if(it->second.get() == oldest){
          std::cout << "[Cache] Evict: " << key << std::endl;
          _cache.erase(it);
          break;
        }
      }
      _fifo.erase(_fifo.begin());
    }
  }
  std::string key = socketEntry->hostname + ":" + std::to_string(socketEntry->port);
  socketEntry->timestamp = _timestamp++;
  SocketEntry*ptr = socketEntry.get();
  _fifo[socketEntry->timestamp] = ptr;
  _cache.emplace(key,std::move(socketEntry));
  std::cout << "[Cache] Store: " << key << " (timestamp: " << ptr->timestamp << ")" << std::endl;
}
void Cache::shutdownSocket(
    std::unique_ptr<SocketEntry> socketEntry,
    unsigned cachedEntries
) {
  (void)cachedEntries;
  if(socketEntry&&socketEntry->fd>=0){
    std::cout << "[Cache] Close: " << socketEntry->hostname 
                  << ":" << socketEntry->port << std::endl;
    close(socketEntry->fd);
    socketEntry->fd = -1;
  }
}
Cache::~Cache() {
    // 清理所有缓存的连接
    clear();
}
//获取顶级域名
std::string_view Cache::tld(std::string_view domain) {
  size_t pos = domain.rfind('.');
  if(pos == std::string_view::npos || pos == domain.size() - 1){
    return domain;
  }
  return domain.substr(pos+1);
}
}
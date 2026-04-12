#include "network/throughput_cache.hpp"
#include <iostream>

namespace myblob::network {

void ThroughputCache::startSocket(int fd) {
  _fdMap[fd] = std::chrono::steady_clock::now();
}

void ThroughputCache::stopSocket(
    std::unique_ptr<SocketEntry> socketEntry,
    uint64_t bytes,
    unsigned cachedEntries,
    bool reuseSocket
) {
  if(!socketEntry){
    return;
  }
  int fd = socketEntry->fd;
  auto it = _fdMap.find(fd);
  if(it != _fdMap.end()){
    auto now = std::chrono::steady_clock::now();
    auto duration=std::chrono::duration_cast<std::chrono::milliseconds>(now-it->second).count();
    double throughput = 0.0;
    if(duration > 0){
      throughput = (static_cast<double>(bytes)/duration)*1000.0;
    }
    if(_throughput.size() < _maxHistory){
      _throughput.push_back(throughput);
    }else{
      _throughput[_throughputIterator%_maxHistory] = throughput;
    }
    _throughputIterator++;
    std::cout << "[ThroughputCache] Socket " << fd 
                  << " throughput: " << throughput << " B/s"
                  << " (bytes: " << bytes << ", duration: " << duration << "ms)"
                  << std::endl;
    _fdMap.erase(it);
    double avgThroughput = getAverageThroughput();
    bool shouldCache = reuseSocket&&(throughput>=avgThroughput*0.8);
    if(shouldCache){
      std::cout << "[ThroughputCache] Caching high-throughput connection"
                      << std::endl;
    }
    Cache::stopSocket(std::move(socketEntry),bytes,cachedEntries,shouldCache);
  }else{
    //没有找到开始时间，直接调用基类
     Cache::stopSocket(std::move(socketEntry),bytes,cachedEntries,reuseSocket);
  }
}
double ThroughputCache::getAverageThroughput() const {
  if(_throughput.empty()){
    return 0.0;
  }
  double sum = 0.0;
  for(double t:_throughput){
    sum+=t;
  }
  return sum/_throughput.size();
}

}
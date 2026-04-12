#pragma once
#include "network/cache.hpp"
#include <vector>
#include <unordered_map>
#include <chrono>

namespace myblob::network {
//功能: 根据连接吞吐量决定是否缓存，高吞吐量连接优先保留
class ThroughputCache : public Cache {
private:
//吞吐量历史记录
  std::vector<double> _throughput;
  //吞吐量迭代器
  uint64_t _throughputIterator = 0;
  //最大历史记录数
  static constexpr unsigned _maxHistory = 128;
  //fd->开始时间的映射
  std::unordered_map<int,std::chrono::steady_clock::time_point> _fdMap;
public:
  ThroughputCache(){
    _throughput.reserve(_maxHistory);
  }
  void startSocket(int fd) override;
  //停止socket并计算吞吐量
  void stopSocket(std::unique_ptr<SocketEntry> socketEntry,uint64_t bytes,unsigned cachedEntries,bool reuseSocket) override;
  //获取平均吞吐量
  double getAverageThroughput() const;
  //获取当前吞吐量历史
  const std::vector<double>& getThroughputHistory()const{
    return _throughput;
  }
};
}
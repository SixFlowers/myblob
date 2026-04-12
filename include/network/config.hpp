#pragma once
#include <cmath>
#include <cstdint>

namespace myblob::network {
struct Config{
  //并发请求数
  static constexpr uint64_t defaultCoreConcurrency = 20;
  //默认吞吐量
  static constexpr uint64_t defaultCoreThroughput = 8000;
  uint64_t coreThroughput;
  unsigned coreConcurrency;
  //网络带宽
  uint64_t network;
  
  [[nodiscard]] constexpr uint64_t bandwidth() const{
    return network;//获取网络带宽
  }
  
  [[nodiscard]] constexpr unsigned coreRequests() const {
    return coreConcurrency;//获取每个核心的并发请求数
  }
  
  [[nodiscard]] constexpr uint64_t retrievers() const{
    return (network + coreThroughput - 1) / coreThroughput;//获取需要的调度器线程数
  }
  
  [[nodiscard]] constexpr uint64_t totalRequests() const {
    return retrievers() * coreRequests(); //获取总并发请求数
  }
};

}

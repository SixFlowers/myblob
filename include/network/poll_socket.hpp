#pragma once
#include "network/socket.hpp"          // 包含 Socket 基类
#include <chrono>                       // 超时时间点
#include <unordered_map>               // fd -> RequestInfo 映射
#include <vector>                     // pollfd 数组
#include <sys/poll.h>                 // POSIX poll 系统调用

namespace myblob::network {
  class PollSocket:public Socket{
  private:
    struct RequestInfo{
      Request* request;
      std::chrono::time_point<std::chrono::steady_clock> timeout;
      int32_t flags;
    };
    std::vector<Request*> ready;
    std::unordered_map<int,RequestInfo> fdToRequest;
    std::vector<pollfd> pollfds;//系统调用poll返回的结果
    int32_t readyFds = 0;//已读取的ready索引
    int32_t submitted = 0;//已提交的请求计数
  public:
    ~PollSocket() override = default;
    bool send(const Request&req,int32_t msgFlags = 0)override;
    bool recv(Request&req,int32_t msgFlags = 0)override;
    bool send_to(Request&req,std::chrono::milliseconds timeout, int32_t msgFlags = 0)override;
    bool recv_to(Request&req,std::chrono::milliseconds timeout, int32_t msgFlags = 0)override;
    Request* complete() override;
    int32_t submit() override;
  private:
  //将fd加入poll监控
    void enqueue(int fd,short events,RequestInfo req);
  };
}
#pragma once
#include "network/socket.hpp"
#include <chrono>
#include <vector>

// 检查是否支持 io_uring
#ifdef MYBLOB_HAS_IO_URING
#include <liburing.h>
#endif
namespace myblob::network {
class IOUringSocket : public Socket{
private:
#ifdef MYBLOB_HAS_IO_URING
  struct io_uring _uring;
  int _eventId = -1;
#endif
  std::vector<Request*> _completions;
public:
  explicit IOUringSocket(uint32_t entries = 1024,int32_t flags = 0);
  ~IOUringSocket() noexcept override;
#ifdef MYBLOB_HAS_IO_URING
    // ====================================================================
    // 准备请求（prep 系列函数）
    // 这些函数只准备请求，不实际提交到内核
    // ====================================================================
    
    /// 准备发送请求
    io_uring_sqe* send_prep(const Request& req, int32_t msg_flags = 0, 
                            uint8_t flags = 0);
    
    /// 准备接收请求
    io_uring_sqe* recv_prep(Request& req, int32_t msg_flags = 0, 
                            uint8_t flags = 0);
    
    /// 准备带超时的发送请求
    io_uring_sqe* send_prep_to(const Request& req, int32_t msg_flags = 0, 
                               uint8_t flags = 0);
    
    /// 准备带超时的接收请求
    io_uring_sqe* recv_prep_to(Request& req, int32_t msg_flags = 0, 
                               uint8_t flags = 0);
#endif

    // ====================================================================
    // Socket 接口实现
    // ====================================================================
    
    /// 发送请求（准备 + 自动提交）
    bool send(const Request& req, int32_t msg_flags = 0) override;
    
    /// 接收请求（准备 + 自动提交）
    bool recv(Request& req, int32_t msg_flags = 0) override;
    
    /// 带超时的发送
    bool send_to(Request& req, std::chrono::milliseconds timeout, 
                 int32_t msg_flags = 0) override;
    
    /// 带超时的接收
    bool recv_to(Request& req, std::chrono::milliseconds timeout, 
                 int32_t msg_flags = 0) override;

    /// 获取完成的请求
    Request* complete() override;

    /// 提交请求到内核
    int32_t submit() override;

#ifdef MYBLOB_HAS_IO_URING
      //批量提交并获取所有完成事件
  uint32_t submitCompleteAll(uint32_t events,std::vector<IOUringSocket::Request*>&completions);
  //查看完成时间
  Request*peek();
  //获取原始完成事件
  io_uring_cqe* completion();
  //标记完成时间为已处理
  void seen(io_uring_cqe*cqe);
  //等待新的完成事件
  void wait();
#endif
};
}
#include "network/io_uring_socket.hpp"
#include <iostream>
#include <cstring>

namespace myblob::network {
/*
用户态                                    内核态
  │                                         │
  │  1. get_sqe() → 取空 sqe                │
  │  2. prep_send() → 填写 sqe             │
  │     sqe = {opcode=SEND, fd=5,           │
  │            addr=buf, len=500}            │
  │     set_data(sqe, &req)                 │
  │                                         │
  │  3. 写入共享内存 SQ                     │
  │     SQ: [sqe0=req1][sqe1=req2]...       │
  │                                         │
  │  4. submit() ──── 系统调用 ──────────→  │
  │                                         │  5. 内核读 SQ
  │                                         │     取 sqe0: SEND fd=5
  │                                         │     取 sqe1: RECV fd=7
  │                                         │
  │                                         │  6. 内核异步执行
  │                                         │     send(fd=5, buf, 500)
  │                                         │     recv(fd=7, buf, 4096)
  │                                         │
  │                                         │  7. 写入 CQ
  │                                         │     CQ: [cqe0={user_data=&req1, res=500}]
  │                                         │          [cqe1={user_data=&req2, res=2048}]
  │                                         │
  │  8. wait_cqe() ←── 读共享内存 ────────  │
  │     cqe0.user_data → &req1              │
  │     req1.length = 500                    │
  │                                         │
  │  9. cqe_seen(cqe0)                      │
  │     释放 CQ 位置                        │
  │                                         │
  │  10. wait_cqe() ←─────────────────────  │
  │     cqe1.user_data → &req2              │
  │     req2.length = 2048                  │
  │  11. cqe_seen(cqe1)                     │
*/
IOUringSocket::IOUringSocket(uint32_t entries, int32_t flags) {
    int ret = io_uring_queue_init(entries, &_uring, flags);
    if (ret < 0) {
        std::cerr << "[ERROR] io_uring_queue_init failed: " 
                  << strerror(-ret) << std::endl;
        throw std::runtime_error("Failed to initialize io_uring");
    }
    std::cout << "[INFO] io_uring initialized with " << entries 
              << " entries" << std::endl;
}

IOUringSocket::~IOUringSocket() noexcept {
    io_uring_queue_exit(&_uring);//  释放共享内存
    std::cout << "[INFO] io_uring destroyed" << std::endl;
}
// send() = send_prep() 的简单包装
bool IOUringSocket::send(const Request& req, int32_t msg_flags) {
    io_uring_sqe* sqe = send_prep(req, msg_flags);
    return sqe != nullptr;
}

bool IOUringSocket::recv(Request& req, int32_t msg_flags) {
    io_uring_sqe* sqe = recv_prep(req, msg_flags);
    return sqe != nullptr;
}

bool IOUringSocket::send_to(Request& req, std::chrono::milliseconds timeout,
                            int32_t msg_flags) {
    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout);
    req.kernelTimeout.tv_sec = nanos.count() / 1000000000;
    req.kernelTimeout.tv_nsec = nanos.count() % 1000000000;
    io_uring_sqe* sqe = send_prep(req, msg_flags, IOSQE_IO_LINK);
    if (!sqe) return false;
    // Link a timeout SQE after the I/O SQE — if the I/O doesn't complete
    // within the timeout, io_uring cancels the linked chain.
    io_uring_sqe* tsqe = io_uring_get_sqe(&_uring);
    if (tsqe) {
        io_uring_prep_link_timeout(tsqe, &req.kernelTimeout, 0);
    }
    return true;
}

bool IOUringSocket::recv_to(Request& req, std::chrono::milliseconds timeout,
                            int32_t msg_flags) {
    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout);
    req.kernelTimeout.tv_sec = nanos.count() / 1000000000;
    req.kernelTimeout.tv_nsec = nanos.count() % 1000000000;
    io_uring_sqe* sqe = recv_prep(req, msg_flags, IOSQE_IO_LINK);
    if (!sqe) return false;
    io_uring_sqe* tsqe = io_uring_get_sqe(&_uring);
    if (tsqe) {
        io_uring_prep_link_timeout(tsqe, &req.kernelTimeout, 0);
    }
    return true;
}

Socket::Request* IOUringSocket::complete() {
    io_uring_cqe* cqe = nullptr;
    int ret = io_uring_peek_cqe(&_uring, &cqe);
    if (ret < 0 || !cqe) {
        // No completion ready — wait briefly to avoid busy-spin.
        // If we've submitted SQEs, io_uring_wait_cqe_timeout blocks until
        // one completes or the timeout expires; otherwise it returns quickly.
        struct __kernel_timespec ts = {.tv_sec = 0, .tv_nsec = 100000}; // 100µs
        ret = io_uring_wait_cqe_timeout(&_uring, &cqe, &ts);
        if (ret < 0 || !cqe) {
            return nullptr;  // still nothing after short wait
        }
    }
    Request* req = static_cast<Request*>(io_uring_cqe_get_data(cqe));
    if (req) {
        req->length = cqe->res;
    }
    io_uring_cqe_seen(&_uring, cqe);
    return req;
}

int32_t IOUringSocket::submit() {
    int ret = io_uring_submit(&_uring);//    //  一次性把 SQ 中所有 sqe 提交给内核
    //  内核异步执行：send/recv/connect 等
    if (ret < 0) {
        std::cerr << "[ERROR] io_uring_submit failed: " 
                  << strerror(-ret) << std::endl;
        return -1;
    }
    return ret;// 返回提交的 sqe 数量
}

io_uring_sqe* IOUringSocket::send_prep(const Request& req, int32_t msg_flags, 
                                       uint8_t flags) {
    io_uring_sqe* sqe = io_uring_get_sqe(&_uring);// 从 SQ 取一个空位
    if (!sqe) {// SQ 满了
        std::cerr << "[ERROR] io_uring_get_sqe failed" << std::endl;
        return nullptr;
    }
    io_uring_prep_send(sqe, req.fd, req.data.sendData, //↑ 填写 sqe：告诉内核"对 fd 执行 send()"
                       static_cast<size_t>(req.length), msg_flags);
    io_uring_sqe_set_data(sqe, const_cast<Request*>(&req));//// 关联 userData，完成时能找到，确保异步顺序
    sqe->flags = flags;
    return sqe;
}

io_uring_sqe* IOUringSocket::recv_prep(Request& req, int32_t msg_flags, 
                                       uint8_t flags) {
    io_uring_sqe* sqe = io_uring_get_sqe(&_uring);
    if (!sqe) {
        std::cerr << "[ERROR] io_uring_get_sqe failed" << std::endl;
        return nullptr;
    }
    io_uring_prep_recv(sqe, req.fd, req.data.recvData, 
                       static_cast<size_t>(req.length), msg_flags);
    io_uring_sqe_set_data(sqe, &req);
    sqe->flags = flags;
    return sqe;
}

io_uring_sqe* IOUringSocket::send_prep_to(const Request& req, int32_t msg_flags, 
                                          uint8_t flags) {
    io_uring_sqe* sqe = send_prep(req, msg_flags, flags);
    return sqe;
}

io_uring_sqe* IOUringSocket::recv_prep_to(Request& req, int32_t msg_flags, 
                                          uint8_t flags) {
    io_uring_sqe* sqe = recv_prep(req, msg_flags, flags);
    return sqe;
}

Socket::Request* IOUringSocket::peek() {
    io_uring_cqe* cqe = nullptr;
    int ret = io_uring_peek_cqe(&_uring, &cqe);  // non-blocking check
    if (ret < 0 || !cqe) {
        return nullptr;
    }
    Request* req = static_cast<Request*>(io_uring_cqe_get_data(cqe));
    if (req) {
        req->length = cqe->res;
    }
    io_uring_cqe_seen(&_uring, cqe);  // mark consumed to prevent double-processing
    return req;
}

io_uring_cqe* IOUringSocket::completion() {
    io_uring_cqe* cqe = nullptr;
    if (io_uring_wait_cqe(&_uring, &cqe) < 0) {// 等待并返回原始 cqe
        return nullptr;
    }
    return cqe;// ⚠️ 不自动 seen()，不转换为 Request*
}
//手动标记 cqe 已处理
void IOUringSocket::seen(io_uring_cqe* cqe) {
    if (cqe) {
        io_uring_cqe_seen(&_uring, cqe);
    }
}

void IOUringSocket::wait() {
    io_uring_wait_cqe(&_uring, nullptr);//// 等待至少一个完成，不取结果
}

uint32_t IOUringSocket::submitCompleteAll(
    uint32_t events, 
    std::vector<IOUringSocket::Request*>& completions) {
    completions.clear();
    int submitted = io_uring_submit(&_uring);// 1. 提交所有 sqe
    if (submitted < 0) {
        return 0;
    }
    io_uring_cqe* cqe;
    unsigned head;
    unsigned count = 0;
    io_uring_for_each_cqe(&_uring, head, cqe) { // 2. 遍历 CQ 中所有完成事件
        if (count >= events) break; // 不超过 events 上限
        Request* req = static_cast<Request*>(io_uring_cqe_get_data(cqe));
        if (req) {
            req->length = cqe->res;// 填写结果
            completions.push_back(req);
        }
        count++;
    }
    io_uring_cq_advance(&_uring, count);// 3. 批量标记已处理
    return count;
}

} // namespace myblob::network

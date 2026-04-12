#include "network/io_uring_socket.hpp"
#include <iostream>
#include <cstring>

namespace myblob::network {

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
    io_uring_queue_exit(&_uring);
    std::cout << "[INFO] io_uring destroyed" << std::endl;
}

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
    io_uring_sqe* sqe = send_prep_to(req, msg_flags);
    return sqe != nullptr;
}

bool IOUringSocket::recv_to(Request& req, std::chrono::milliseconds timeout, 
                            int32_t msg_flags) {
    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout);
    req.kernelTimeout.tv_sec = nanos.count() / 1000000000;
    req.kernelTimeout.tv_nsec = nanos.count() % 1000000000;
    io_uring_sqe* sqe = recv_prep_to(req, msg_flags);
    return sqe != nullptr;
}

Socket::Request* IOUringSocket::complete() {
    io_uring_cqe* cqe = nullptr;
    int ret = io_uring_peek_cqe(&_uring, &cqe);
    if (ret < 0 || !cqe) {
        return nullptr;
    }
    Request* req = static_cast<Request*>(io_uring_cqe_get_data(cqe));
    io_uring_cqe_seen(&_uring, cqe);
    return req;
}

int32_t IOUringSocket::submit() {
    int ret = io_uring_submit(&_uring);
    if (ret < 0) {
        std::cerr << "[ERROR] io_uring_submit failed: " 
                  << strerror(-ret) << std::endl;
        return -1;
    }
    return ret;
}

io_uring_sqe* IOUringSocket::send_prep(const Request& req, int32_t msg_flags, 
                                       uint8_t flags) {
    io_uring_sqe* sqe = io_uring_get_sqe(&_uring);
    if (!sqe) {
        std::cerr << "[ERROR] io_uring_get_sqe failed" << std::endl;
        return nullptr;
    }
    io_uring_prep_send(sqe, req.fd, req.data.sendData, 
                       static_cast<size_t>(req.length), msg_flags);
    io_uring_sqe_set_data(sqe, const_cast<Request*>(&req));
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
    int ret = io_uring_peek_cqe(&_uring, &cqe);
    if (ret < 0 || !cqe) {
        return nullptr;
    }
    return static_cast<Request*>(io_uring_cqe_get_data(cqe));
}

io_uring_cqe* IOUringSocket::completion() {
    io_uring_cqe* cqe = nullptr;
    io_uring_peek_cqe(&_uring, &cqe);
    return cqe;
}

void IOUringSocket::seen(io_uring_cqe* cqe) {
    if (cqe) {
        io_uring_cqe_seen(&_uring, cqe);
    }
}

void IOUringSocket::wait() {
    io_uring_wait_cqe(&_uring, nullptr);
}

uint32_t IOUringSocket::submitCompleteAll(
    uint32_t events, 
    std::vector<IOUringSocket::Request*>& completions) {
    completions.clear();
    int submitted = io_uring_submit(&_uring);
    if (submitted < 0) {
        return 0;
    }
    io_uring_cqe* cqe;
    unsigned head;
    unsigned count = 0;
    io_uring_for_each_cqe(&_uring, head, cqe) {
        if (count >= events) break;
        Request* req = static_cast<Request*>(io_uring_cqe_get_data(cqe));
        if (req) {
            completions.push_back(req);
        }
        count++;
    }
    io_uring_cq_advance(&_uring, count);
    return count;
}

} // namespace myblob::network

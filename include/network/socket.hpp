#pragma once
#include <chrono>
#include <cstdint>

// 自动检测 io_uring 支持
#ifdef __has_include
#if __has_include(<liburing.h>)
#define MYBLOB_HAS_IO_URING 1
#include <liburing.h>
#endif
#endif

namespace myblob::network {
class Socket{
public:
    enum class EventType : uint8_t {
        read = 0,
        write = 1
    };
    
    struct Request {
        union Data {
            const uint8_t* sendData;
            uint8_t* recvData;
        };
        Data data;
        int64_t length;
        int32_t fd;
        EventType event;
        void* userData;

        // ✅ 关键：io_uring 需要的内核级超时
        // 只在支持 io_uring 时编译
        #ifdef MYBLOB_HAS_IO_URING
        __kernel_timespec kernelTimeout = {.tv_sec = 0, .tv_nsec = 0};
        #endif
    };
    
    virtual ~Socket() = default;
    virtual bool send(const Request& req, int32_t msgFlags = 0) = 0;
    virtual bool recv(Request& req, int32_t msgFlags = 0) = 0;
    virtual bool send_to(Request& req, std::chrono::milliseconds timeout, int32_t msgFlags = 0) = 0;
    virtual bool recv_to(Request& req, std::chrono::milliseconds timeout, int32_t msgFlags = 0) = 0;
    virtual Request* complete() = 0;
    virtual int32_t submit() = 0;
};
}

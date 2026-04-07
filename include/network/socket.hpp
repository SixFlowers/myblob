#pragma once
#include <chrono>
#include <cstdint>

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

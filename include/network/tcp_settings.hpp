#pragma once
#include <chrono>

namespace myblob::network {

struct TCPSettings {
    int nonBlocking = 1;
    int noDelay = 0;
    int keepAlive = 1;
    int keepIdle = 1;
    int keepIntvl = 1;
    int keepCnt = 1;
    int recvBuffer = 0;
    int mss = 0;
    int reusePorts = 0;
    int linger = 1;
    std::chrono::milliseconds timeout = std::chrono::milliseconds(500);
    int reuse = 1;
};

}

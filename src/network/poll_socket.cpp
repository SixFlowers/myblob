#include "network/poll_socket.hpp"
#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace myblob::network {

void PollSocket::enqueue(int fd, short events, RequestInfo req) {
    pollfd pfd = {};
    pfd.fd = fd;
    pfd.events = events;
    pfd.revents = 0;
    pollfds.push_back(pfd);
    fdToRequest[fd] = std::move(req);
    submitted++;
}

bool PollSocket::send(const Request& req, int32_t msgFlags) {
    RequestInfo info = {};
    info.request = const_cast<Request*>(&req);
    info.flags = msgFlags;
    info.timeout = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    enqueue(req.fd, POLLOUT, std::move(info));
    return true;
}

bool PollSocket::recv(Request& req, int32_t msgFlags) {
    RequestInfo info = {};
    info.request = const_cast<Request*>(&req);
    info.flags = msgFlags;
    info.timeout = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    enqueue(req.fd, POLLIN, std::move(info));
    return true;
}

bool PollSocket::send_to(Request& req, std::chrono::milliseconds timeout, int32_t msgFlags) {
    RequestInfo info = {};
    info.request = const_cast<Request*>(&req);
    info.flags = msgFlags;
    info.timeout = std::chrono::steady_clock::now() + timeout;
    enqueue(req.fd, POLLOUT, std::move(info));
    return true;
}

bool PollSocket::recv_to(Request& req, std::chrono::milliseconds timeout, int32_t msgFlags) {
    RequestInfo info = {};
    info.request = const_cast<Request*>(&req);
    info.flags = msgFlags;
    info.timeout = std::chrono::steady_clock::now() + timeout;
    enqueue(req.fd, POLLIN, std::move(info));
    return true;
}

Socket::Request* PollSocket::complete() {
    if(readyFds >= static_cast<int32_t>(ready.size())){
      return nullptr;
    }
    return ready[readyFds++];
}

int32_t PollSocket::submit() {
    if(pollfds.empty()){
      return 0;
    }
    int timeoutMs = 1000;
    int n = ::poll(pollfds.data(), pollfds.size(), timeoutMs);
    if(n < 0){
      return -1;
    }
    ready.clear();
    readyFds = 0;
    for(size_t i = 0; i < pollfds.size(); i++){
      if(pollfds[i].revents){
        int fd = pollfds[i].fd;
        auto it = fdToRequest.find(fd);
        if(it != fdToRequest.end()){
          ready.push_back(it->second.request);
          fdToRequest.erase(it);
        }
        pollfds[i] = pollfds.back();
        pollfds.pop_back();
        --i;
      }
    }
    return static_cast<int32_t>(ready.size());
}

}

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
    // Calculate minimum timeout from pending requests
    int timeoutMs = 1000;
    auto now = std::chrono::steady_clock::now();
    for (const auto& pair : fdToRequest) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            pair.second.timeout - now).count();
        if (remaining < 0) remaining = 0;
        if (remaining < timeoutMs) timeoutMs = static_cast<int>(remaining);
    }
    int n;
    do {
      n = ::poll(pollfds.data(), pollfds.size(), timeoutMs);
    } while (n < 0 && errno == EINTR);  // retry on signal interrupt
    if(n < 0){
      return -1;
    }
    ready.clear();
    readyFds = 0;

    // Process ready fds and timed-out requests
    for(size_t i = 0; i < pollfds.size(); i++){
      int fd = pollfds[i].fd;
      auto it = fdToRequest.find(fd);
      if(it == fdToRequest.end()){
        // Orphan fd: should not happen, but clean up defensively
        pollfds[i] = pollfds.back();
        pollfds.pop_back();
        --i;
        continue;
      }

      auto* req = it->second.request;
      auto flags = it->second.flags;
      bool handled = false;

      if(pollfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)){
        req->length = -ECONNRESET;
        handled = true;
      } else if(pollfds[i].revents & POLLOUT){
        // fd writable: perform actual send
        ssize_t result = ::send(fd, req->data.sendData,
                                static_cast<size_t>(req->length), flags);
        req->length = static_cast<int64_t>(result);
        handled = true;
      } else if(pollfds[i].revents & POLLIN){
        // fd readable: perform actual recv
        ssize_t result = ::recv(fd, req->data.recvData,
                                static_cast<size_t>(req->length), flags);
        req->length = static_cast<int64_t>(result);
        handled = true;
      } else if (n == 0) {
        // poll timed out — check each request's deadline
        if (now >= it->second.timeout) {
          req->length = -ETIMEDOUT;
          handled = true;
        }
      }

      if(handled){
        ready.push_back(req);
        fdToRequest.erase(it);
        // swap-and-pop to remove from pollfds
        pollfds[i] = pollfds.back();
        pollfds.pop_back();
        --i;
      }
      // unhandled fds stay in pollfds for next submit() call
    }
    return static_cast<int32_t>(ready.size());
}

}

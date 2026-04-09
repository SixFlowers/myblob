#pragma once
#include <array>              // std::array 容器
#include <cstdint>           // uint8_t, uint64_t 等类型
#include <openssl/ssl.h>     // SSL_CTX, SSL_SESSION 等
#include <openssl/types.h>    // OpenSSL 类型定义

namespace myblob::network {
  class TLSContext{
  public:
    //会话缓存大小
    static constexpr uint8_t cachePower = 8;
    //缓存掩码
    static constexpr uint64_t cacheMask = (~0ull) >> (64 - cachePower);
    //数组大小
    static constexpr uint64_t cacheSize = 1ull >> cachePower;
    TLSContext();
    ~TLSContext();
    bool cacheSession(int fd,SSL* ssl);
    bool dropSession(int fd);
    bool reuseSession(int fd,SSL*ssl);
    static void initOpenSSL();
    friend class TLSConnection;
    [[nodiscard]] inline SSL_CTX* getContext()const{
      return _ctx;
    }
  private:
    SSL_CTX* _ctx;
    std::array<std::pair<uint64_t,SSL_SESSION*>,cacheSize> _sessionCache;
  };
}
#include "network/tls_context.hpp"
#include <netinet/in.h>         // sockaddr_in, getpeername()
#include <openssl/crypto.h>     // OpenSSL 加密函数
#include <openssl/ssl.h>        // SSL, SSL_SESSION
#include <sys/socket.h>         // socket 系统调用
#include <cstring>              // memset

namespace myblob::network {
TLSContext::TLSContext() : _sessionCache(){
  auto method = TLS_client_method();
  _ctx = SSL_CTX_new(method);
  //弃用客户端会话缓存
  SSL_CTX_set_session_cache_mode(_ctx,SSL_SESS_CACHE_CLIENT);
}
TLSContext::~TLSContext(){
  for(uint64_t i = 0ull;i < cacheSize;i++){
    if(_sessionCche[i].first && _sessionCache[i].second){
      SSL_SESSION_free(_sessionCache[i].second);
    }
  }
  if(_ctx){
    SSL_CTX_free(_ctx);
  }
}
void TLSContext::initOpenSSL() {//static方法只调用一次;只会初始化一次
  OPENSSL_add_ssl_algorithms();
  SSL_load_error_strings();

}
//通过本地socket获取对端ip
bool TLSContext::cacheSession(int fd, SSL* ssl) {
  if(SSL_session_reused(ssl)){
    return false;
  }
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof(struct sockaddr_in);
  if(!getpeername(fd,reinterpret_cast<struct sockaddr*>(&addr),&addrlen)){
    uint64_t index = addr.sin_addr.s_addr & cacheMask;
    if(_sessionCache[index].first){
      SSL_SESION_free(_sessionCache[index].second);
    }
    _sessionCache[index] = std::make_pair(addr.sin_addr.s_addr,SSL_get1_session(ssl));
    return true;
  }
  return false;

}
bool TLSContext::dropSession(int fd) {
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof(struct sockaddr_in);
  if(!getpeername(fd,reinterpret_cast<struct sockaddr*>(&addr),&addrlen)){
    uint64_t index = addr.sin_addr.s_addr & cacheMask;
    if(_sessionCache[index].first){
      auto session = _sessionCache[index].second;
      _sessionCache[index].first = 0;
      SSL_SESSION_free(session);
    }
    return true;
  }
  return false;
}
bool TLSContext::reuseSession(int fd, SSL* ssl){
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof(struct sockaddr_in);
  if(!getpeername(fd,reinterpret_cast<struct sockaddr*>(&addr),&addrlen)){
    auto& pair = _sessionCache[addr.sin_addr.s_addr & cacheMask];
    if(pair.first == addr.sin_addr.s_addr){
      SSL_set_session(ssl,pair.second);
      return true;
    }
  }
  return false;
}
}
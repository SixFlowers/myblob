#pragma once
#include <memory>               // std::unique_ptr
#include <cstdint>            // uint8_t, int64_t 等类型
#include <openssl/types.h>    // OpenSSL 类型定义

namespace myblob::network {
class TLSContext;
class ConnectionManager;
struct HTTPSMessage;
class TLSConnection {
public:
  enum class Progress : uint16_t{
    Init,
    SendingInit,
    Sending,
    ReceivingInit,
    Receiving,
    Progress, //进行中
    Finished, 
    Aborted // 中止/失败
  };
  struct State{
    size_t internalBioWrite;//发送待写入字节数
    int64_t networkBioRead;//发送已读取字节数
    size_t socketWrite;//发送已写入socket字节数
    int64_t networkBioWrite //接收已写入字节数
    size_t internalBioRead//接收待读取字节数
    size_t socketRead // 接收已丛socket读取字节数
    Progress progress;
    inline void reset(){
      internalBioWrite = 0;
      networkBioRead = 0;
      socketWrite = 0;
      internalBioRead = 0;
      networkBioWrite = 0;
      socketRead = 0;
    }
  };
  TLSConnection(TLSContext&context);
  ~TLSConnection();
  bool init(HTTPSMessage* message);
  void destroy();
  [[nodiscard]] inline TLSContext& getContext()const{
    return _context;
  }
  //通过TLS发送接收数据
  [[nodiscard]] Progress recv(ConnectionManager&ConnectionManager,char*buffer,int64_t bufferLength,int64_t&resultLength);
  [[nodiscard]] Progress send(
        ConnectionManager& connectionManager,
        const char* buffer,
        int64_t bufferLength,
        int64_t& resultLength
    );
    //执行TLS握手
  [[nodiscard]] Progress connect(ConnectionManager&connectionManager);
  //TLS关闭
  [[nodiscard]] Progress shutdown(ConnectionManager&connectionMannager,bool failedOnce = false);
private:
  //辅助函数
  template <typename F>
  Progress operationHleper(ConnectionManager&connectionManager,F&& func,int64_t& result);
  //处理TLS层数据传输
  Progress progress(ConnectionManager&connectionManager);
  //关联https消息
  HTTPSMessage *_message;
  //TLS上下文引用
  TLSContext& _context;
  //OpenSSL SSL句柄
  SSL* _ssl;
  //内部BIO(与应用程序交互)
  BIO* _internalBio;
  //网络BIO(与socket交互)
  BIO* _netwrokBio;
  //临时缓冲区
  std::unique_ptr<char[]> _buffer;
  //当前状态
  State _state;
  //是否已连接
  bool _connected;

};

}

#pragma once
#include <memory>
#include <cstdint>
#include <openssl/types.h>

namespace myblob::network {

class TLSContext;
class ConnectionManager;
struct HTTPSMessage;

class TLSConnection {
public:
  enum class Progress : uint16_t {
    Init,
    SendingInit,
    Sending,
    ReceivingInit,
    Receiving,
    InProgress,
    Finished, 
    Aborted
  };

  struct State {
    size_t internalBioWrite;    //发送待写入字节数
    int64_t networkBioRead;     //发送已读取字节数
    size_t socketWrite;         //发送已写入socket字节数
    int64_t networkBioWrite;    //接收已写入字节数
    size_t internalBioRead;     //接收待读取字节数
    size_t socketRead;          //接收已从socket读取字节数
    Progress progress;

    inline void reset() {
      internalBioWrite = 0;
      networkBioRead = 0;
      socketWrite = 0;
      internalBioRead = 0;
      networkBioWrite = 0;
      socketRead = 0;
    }
  };

  TLSConnection(TLSContext& context);
  ~TLSConnection();
  bool init(HTTPSMessage* message);
  void destroy();

  [[nodiscard]] inline TLSContext& getContext() const {
    return _context;
  }

  //通过TLS发送接收数据
  [[nodiscard]] Progress recv(ConnectionManager& connectionManager, char* buffer, int64_t bufferLength, int64_t& resultLength);
  [[nodiscard]] Progress send(ConnectionManager& connectionManager, const char* buffer, int64_t bufferLength, int64_t& resultLength);
  //执行TLS握手
  [[nodiscard]] Progress connect(ConnectionManager& connectionManager);
  //TLS关闭
  [[nodiscard]] Progress shutdown(ConnectionManager& connectionManager, bool failedOnce = false);

private:
  //辅助函数
  template <typename F>
  Progress operationHelper(ConnectionManager& connectionManager, F&& func, int64_t& result);
  //处理TLS层数据传输
  Progress process(ConnectionManager& connectionManager);

  HTTPSMessage* _message;
  TLSContext& _context;
  SSL* _ssl;
  BIO* _internalBio;
  BIO* _networkBio;
  std::unique_ptr<char[]> _buffer;
  State _state;
  bool _connected;
};

}

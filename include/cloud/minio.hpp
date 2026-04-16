#pragma once
#include "cloud/aws.hpp"

namespace myblob {
namespace network {
class TaskedSendReceiver;
} // namespace network

namespace cloud {
class MinIO:public AWS{
public:
  explicit MinIO(const RemoteInfo&info,myblob::network::ConnectionManager&conn_mgr,myblob::network::HttpClient&http_client):AWS(info,conn_mgr,http_client){
    assert(info.provider == CloudService::MinIO);
  }
  MinIO(const RemoteInfo& info, 
          const std::string& keyId, 
          const std::string& key,
          myblob::network::ConnectionManager& conn_mgr,
          myblob::network::HttpClient& http_client):AWS(info,keyId,key,conn_mgr,http_client){}
  //获取服务器地址
  [[nodiscard]] std::string getAddress() const override;
/// 获取实例详情
    [[nodiscard]] Provider::Instance getInstanceDetails(
        myblob::network::TaskedSendReceiverHandle& sendReceiver) override;
  //设置多部份上传分篇大小
  constexpr void setMultipartUploadSize(uint64_t size){
    _multipartUploadSize = size;
  }
  friend Provider;
};
}
}
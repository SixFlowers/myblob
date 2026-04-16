#pragma once
#include "cloud/provider.hpp"
#include <cassert>
#include <string>

namespace myblob {
namespace network {
class TaskedSendReceiver;
class TaskedSendReceiverHandle;
} // namespace network

namespace cloud {
class Azure: public Provider{
public:
  //Azure设置结构
  struct Settings{
    std::string container; //容器名称
    uint32_t port;
  };
  //Azure 密钥结构
  struct Secret{
    std::string accountName;//账户名称
    std::string privateKey;//私钥(用于签名)
  };
  //测试用固定时间戳
  const char* fakeXMSTimestamp = "Fri, 01 Jan 2100 00:00:00 GMT";
protected:
  Settings _settings;
  std::unique_ptr<Secret> _secret;
  //初始化密钥
  void initSecret(myblob::network::TaskedSendReceiverHandle&sendReceiverHandle) override;
  //获取密钥副本
  void getSecret() override;
  //从文件初始化密钥
  void initKey();
public:
//从IMDS获取凭证
  Azure(const RemoteInfo&info,myblob::network::ConnectionManager& conn_mgr,
  myblob::network::HttpClient&http_client):Provider(info.endpoint,info.port,conn_mgr,http_client,info.provider),
  _settings({info.bucket,info.port}){
    assert(info.provider == CloudService::Azure);
    _type = info.provider;
  }
  //显示密钥
  Azure(const RemoteInfo&info,const std::string&accountName,const
  std::string&accountKey,myblob::network::ConnectionManager& conn_mgr,
  myblob::network::HttpClient&http_client):Azure(info,conn_mgr,http_client){
    _secret = std::make_unique<Secret>();
    _secret->accountName = accountName;
    _secret->privateKey = accountKey;
    initKey();
  }
  //获取实例详情
  [[nodiscard]] Provider::Instance getInstanceDetails(myblob::network::TaskedSendReceiverHandle&sendReceiver)override;
  //获取实例区域
  [[nodiscard]] static std::string getInstanceRegion(myblob::network::TaskedSendReceiverHandle&sendReceiver);
  //初始化缓存
  void initCache(myblob::network::TaskedSendReceiverHandle&sendReceiverHandle) override{}
private:
  //获取设置
  [[nodiscard]] inline Settings getSettings(){
    return _settings;
  }
  //构建GET请求
  [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> getRequest(
    const std::string&filePath,
    const std::pair<uint64_t,uint64_t>&range
  ) const override;
  //构建PUT请求
  [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> putRequest(
    const std::string&filePath,
    std::string_view object
  ) const override;
  //构建DELETE请求
  [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> deleteRequest(
    const std::string&filePath
  ) const override;
    /// 构建通用 PUT 请求（支持多部分上传）
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> putRequestGeneric(
        const std::string& filePath, 
        std::string_view object, 
        uint16_t part, 
        std::string_view uploadId) const override;
    
    /// 构建创建多部分上传请求
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> createMultiPartRequest(
        const std::string& filePath) const override;
    
    /// 构建完成多部分上传请求
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> completeMultiPartRequest(
        const std::string& filePath, 
        std::string_view uploadId, 
        const std::vector<std::string>& etags, 
        std::string& content) const override;
    
    /// 重新签名请求
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> resignRequest(
        const utils::DataVector<uint8_t>& data, 
        const uint8_t* bodyData = nullptr, 
        uint64_t bodyLength = 0) const override;
    
    /// 获取多部分上传大小
    [[nodiscard]] uint64_t multipartUploadSize() const override { return 128ull << 20; }

    /// 获取服务器地址
    [[nodiscard]] std::string getAddress() const override;
    
    /// 获取服务器端口
    [[nodiscard]] uint16_t getPort() const override;

    /// 构建实例信息请求
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> downloadInstanceInfo() const;
    
    /// 获取 IAM 地址（Azure 实例元数据服务）
    [[nodiscard]] static constexpr std::string_view getIAMAddress() {
        return "169.254.169.254";
    }
    
    /// 获取 IAM 端口
    [[nodiscard]] static constexpr uint32_t getIAMPort() {
        return 80;
    }

    friend Provider;
};
}
}
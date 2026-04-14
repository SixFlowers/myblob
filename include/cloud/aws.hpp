#pragma once
#include "cloud/aws_signer.hpp"
#include "cloud/provider.hpp"
#include "utils/data_vector.hpp"
#include <cassert>
#include <memory>
#include <mutex>
#include <string>

namespace myblob {

namespace network {
class ConnectionManager;
class HttpClient;
class TaskedSendReceiver;
class TaskedSendReceiverHandle;
}

namespace cloud {
  class AWS:public Provider{
  public:
    struct Settings{
      std::string bucket;
      std::string region;
      std::string endpoint;
      uint16_t port = 80;
      bool zonal = false;
    };
    struct Secret{//AWS凭证
      //IAM用户名
      std::string iamUser;
      //Access keyID
      std::string keyId;
      //Secret Access Key
      std::string secret;
      //会话令牌(临时凭证)
      std::string token;
      //过期时间
      int64_t expiration = 0;
    };
    //测试用固定时间戳AMZ格式
    const char* fakeAMZTimestamp = "21000101T000000Z";
    //测试用固定时间戳IAM格式
    const char* fakeIAMTimestamp = "2100-01-01T00:00:00Z";
  protected:
    Settings _settings;
    //全局凭证
    std::shared_ptr<Secret> _globalSecret;
    //全局会话凭证
    std::shared_ptr<Secret> _globalSessionSecret;
    //多部分上传分片大小
    uint64_t _multipartUploadSize = 128ull << 20;
    //凭证互斥锁
    std::mutex _mutex;
    //线程本地凭证
    thread_local static std::shared_ptr<Secret> _secret;
    //线程本会话凭证
    thread_local static std::shared_ptr<Secret>_sessionSecret;
    //有效实例指针
    thread_local static AWS* _validInstance;
    public:
    explicit AWS(const RemoteInfo& info, 
                 network::ConnectionManager& conn_mgr,
                 network::HttpClient& http_client) 
        : Provider(info.endpoint, info.port, conn_mgr, http_client, info.provider)
        , _settings({info.bucket, info.region, info.endpoint, info.port, info.zonal})
        , _mutex() {
          assert(info.provider == CloudService::AWS || info.provider == 
          CloudService::MinIO || info.provider == CloudService::Oracle || 
        info.provider == CloudService::IBM);
        //指定存储桶
        assert(!info.bucket.empty());
        assert(info.provider == CloudService::AWS || (!info.endpoint.empty()||
      !info.region.empty()));
      _type = info.provider;
    }
    AWS(const RemoteInfo& info, 
        const std::string& keyId, 
        const std::string& key,
        network::ConnectionManager& conn_mgr,
        network::HttpClient& http_client):AWS(info, conn_mgr, http_client){
      _globalSecret = std::make_shared<Secret>();
      _globalSecret->keyId = keyId;
      _globalSecret->secret = key;
      _secret = _globalSecret;
    }
    //获取实例详细
    [[nodiscard]] Provider::Instance getInstanceDetails(myblob::network::TaskedSendReceiverHandle& sendReceiver) override;
    //获取实例区域
    [[nodiscard]] static std::string getInstanceRegion(myblob::network::TaskedSendReceiverHandle& sendReceiver);
    //初始化缓存
    void initCache(myblob::network::TaskedSendReceiverHandle& sendReceiverHandle) override;
  private:
    //初始化凭证(从IAM元数据服务获取)
    void initSecret(myblob::network::TaskedSendReceiverHandle& sendReceiverHandle) override;
    //获取本地凭证副本
    void getSecret() override;
    //下载IAM用户信息
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> downloadIAMUser() const;
    //下载密钥
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> downloadSecret(std::string_view content, std::string& iamUser);
    //更新密钥
    bool updateSecret(std::string_view content, std::string_view iamUser);
    /// 更新会话令牌
    bool updateSessionToken(std::string_view content);
    /// 检查密钥是否有效
    /// @param offset 提前量（秒），默认 60 秒
    [[nodiscard]] bool validKeys(uint32_t offset = 60) const;
    /// 检查会话是否有效
    [[nodiscard]] bool validSession(uint32_t offset = 60) const;
    /// 获取设置
    [[nodiscard]] inline Settings getSettings() { return _settings; }
    /// 允许多部分上传如果大小 > 0
    [[nodiscard]] uint64_t multipartUploadSize() const override { return _multipartUploadSize; }
    
    /// 创建通用HTTP请求并签名
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> buildRequest(network::HttpRequest& request, const uint8_t* bodyData = nullptr, uint64_t bodyLength = 0, bool initHeaders = true) const;
    /// 重新签名请求
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> resignRequest(const utils::DataVector<uint8_t>& data, const uint8_t* bodyData = nullptr, uint64_t bodyLength = 0) const override;
    /// 构建下载blob或列出目录的HTTP请求
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> getRequest(const std::string& filePath, const std::pair<uint64_t, uint64_t>& range) const override;
    /// 构建上传对象的HTTP请求（不包含对象数据本身）
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> putRequestGeneric(const std::string& filePath, std::string_view object, uint16_t part, std::string_view uploadId) const override;
    /// 构建上传对象的HTTP请求
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> putRequest(const std::string& filePath, std::string_view object) const override {
        return putRequestGeneric(filePath, object, 0, "");
    }
    // 构建删除对象的HTTP请求
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> deleteRequest(const std::string& filePath) const override {
        return deleteRequestGeneric(filePath, "");
    }
    /// 构建删除对象的HTTP请求
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> deleteRequestGeneric(const std::string& filePath, std::string_view uploadId) const;
    /// 构建创建多部分上传的HTTP请求
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> createMultiPartRequest(const std::string& filePath) const override;
    /// 构建完成多部分上传的HTTP请求
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> completeMultiPartRequest(const std::string& filePath, std::string_view uploadId, const std::vector<std::string>& etags, std::string& content) const override;
    /// 获取IAM地址
    [[nodiscard]] static std::string getIAMAddress();
    /// 获取IAM端口
    [[nodiscard]] static uint32_t getIAMPort();
    /// 获取会话令牌
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> getSessionToken(std::string_view type = "ReadWrite") const;
    
  public:
    /// 获取端口
    [[nodiscard]] uint16_t getPort() const override;
    /// 获取地址
    [[nodiscard]] std::string getAddress() const override;
    
  };
}
}

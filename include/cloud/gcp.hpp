#pragma once
#include "cloud/provider.hpp"
#include "utils/data_vector.hpp"
#include <cassert>
#include <string>

namespace myblob {
namespace network {
class TaskedSendReceiver;
class TaskedSendReceiverHandle;
} // namespace network

namespace cloud {
class GCP:public Provider{
  public:
  struct Settings{
    std::string bucket;
    std::string region;
    uint32_t port;
  };
  struct Secret{
    std::string serviceAccountEmail;
    std::string privateKey;
  };
protected:
    Settings _settings;
    std::unique_ptr<Secret> _secret;

    const char* fakeAMZTimestamp = "20250101T000000Z";

    /// 初始化密钥（从GCP元数据服务获取）
    void initSecret(myblob::network::TaskedSendReceiverHandle& sendReceiverHandle) override;
    
    /// 获取密钥副本
    void getSecret() override;
public:
    /// 构造函数（从元数据服务获取凭证）
    /// @param info 远程信息
    /// @param conn_mgr 连接管理器
    /// @param http_client HTTP客户端
    GCP(const RemoteInfo& info,
        myblob::network::ConnectionManager& conn_mgr,
        myblob::network::HttpClient& http_client)
        : Provider(info.endpoint, info.port, conn_mgr, http_client, info.provider)
        , _settings({info.bucket, info.region, info.port}) {
        assert(info.provider == CloudService::GCP);
        _type = info.provider;
    }

    /// 构造函数（显式密钥）
    /// @param info 远程信息
    /// @param clientEmail 服务账户邮箱
    /// @param key 私钥
    /// @param conn_mgr 连接管理器
    /// @param http_client HTTP客户端
    GCP(const RemoteInfo& info, 
        const std::string& clientEmail, 
        const std::string& key,
        myblob::network::ConnectionManager& conn_mgr,
        myblob::network::HttpClient& http_client)
        : GCP(info, conn_mgr, http_client) {
        _secret = std::make_unique<Secret>();
        _secret->serviceAccountEmail = clientEmail;
        _secret->privateKey = key;
    }

    /// 获取实例详情
    [[nodiscard]] Provider::Instance getInstanceDetails(
        myblob::network::TaskedSendReceiverHandle& sendReceiver) override;
    
    /// 获取实例区域
    [[nodiscard]] static std::string getInstanceRegion(
        myblob::network::TaskedSendReceiverHandle& sendReceiver);

    /// 初始化缓存
    void initCache(myblob::network::TaskedSendReceiverHandle& sendReceiverHandle) override {}

private:
    /// 获取设置
    [[nodiscard]] inline Settings getSettings() { return _settings; }
    
    /// 多部分上传分片大小（128MB）
    [[nodiscard]] uint64_t multipartUploadSize() const override { 
        return 128ull << 20; 
    }

    /// 构建 GET 请求
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> getRequest(
        const std::string& filePath, 
        const std::pair<uint64_t, uint64_t>& range) const override;
    
    /// 构建通用 PUT 请求（支持多部分上传）
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> putRequestGeneric(
        const std::string& filePath, 
        std::string_view object, 
        uint16_t part, 
        std::string_view uploadId) const override;
    
    /// 构建 PUT 请求
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> putRequest(
        const std::string& filePath, 
        std::string_view object) const override {
        return putRequestGeneric(filePath, object, 0, "");
    }
    
    /// 构建 DELETE 请求
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> deleteRequest(
        const std::string& filePath) const override {
        return deleteRequestGeneric(filePath, "");
    }
    
    /// 构建通用 DELETE 请求
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> deleteRequestGeneric(
        const std::string& filePath, 
        std::string_view uploadId) const;
    
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

    /// 获取服务器地址
    [[nodiscard]] std::string getAddress() const override;
    
    /// 获取服务器端口
    [[nodiscard]] uint16_t getPort() const override;

    /// 构建实例信息请求
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> downloadInstanceInfo(
        const std::string& info = "machine-type") const;
    
    /// 获取 IAM 地址（GCP 元数据服务）
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
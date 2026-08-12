#pragma once
#include "cloud_service.hpp"
#include "../network/http_client.hpp"
#include "../network/http_response.hpp"
#include "../utils/data_vector.hpp"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace myblob {

namespace network {
class ConnectionManager;
class TaskedSendReceiverHandle;
struct Config;
}

namespace cloud {

enum class CloudService : uint8_t;
class AWS;
class Transaction;

class Provider {
public:
    /// 远程协议前缀数量
    static constexpr unsigned remoteFileCount = 8;
    
    /// 远程协议前缀数组
    static constexpr std::string_view remoteFile[] = {
        "https://",     // HTTPS = 0
        "http://",      // HTTP = 1
        "s3://",        // AWS = 2
        "azure://",     // Azure = 3  ✅ 新增
        "gs://",        // GCP = 4     ✅ 新增
        "oci://",       // Oracle = 5
        "ibm://",       // IBM = 6
        "minio://"      // MinIO = 7   ✅ 新增
    };
    // 使用cloud_service.hpp中定义的CloudService
    using CloudService = myblob::cloud::CloudService;

    // 实例信息结构体
    struct Instance {//用于获取当前运行环境的云实例信息
        std::string region;
        std::string zone;
        std::string type;
        std::string id;
        std::string address;
        uint16_t port = 0;
    };

    // 静态成员
    static bool testEnvironment;

    // 构造函数
    Provider(myblob::network::ConnectionManager& conn_mgr,
             myblob::network::HttpClient& http_client,
             CloudService type);//不需要立即指定服务器地址，地址在后续通过其他方式设置
    
    Provider(const std::string& addr, uint16_t port,
             myblob::network::ConnectionManager& conn_mgr,
             myblob::network::HttpClient& http_client,
             CloudService type);

    virtual ~Provider() = default;

    // 纯虚函数
    //每个云服务商的 HTTP 请求长得完全不一样，基类没法给出有意义的默认实现。
    virtual std::unique_ptr<utils::DataVector<uint8_t>> getRequest(
        const std::string& filePath,
        const std::pair<uint64_t, uint64_t>& range = {0, 0}
    ) const = 0;
    
    virtual std::unique_ptr<utils::DataVector<uint8_t>> putRequest(
        const std::string& filePath,
        std::string_view object
    ) const = 0;
    
    virtual std::unique_ptr<utils::DataVector<uint8_t>> deleteRequest(
        const std::string& filePath
    ) const = 0;

    // AWS需要的额外虚函数
    virtual std::unique_ptr<utils::DataVector<uint8_t>> putRequestGeneric(
        const std::string& filePath,
        std::string_view object,
        uint16_t part,
        std::string_view uploadId
    ) const = 0;
    
    virtual std::unique_ptr<utils::DataVector<uint8_t>> createMultiPartRequest(
        const std::string& filePath
    ) const = 0;
    
    virtual std::unique_ptr<utils::DataVector<uint8_t>> completeMultiPartRequest(
        const std::string& filePath,
        std::string_view uploadId,
        const std::vector<std::string>& etags,
        std::string& content
    ) const = 0;
    
    virtual std::unique_ptr<utils::DataVector<uint8_t>> resignRequest(
        const utils::DataVector<uint8_t>& data,
        const uint8_t* bodyData = nullptr,
        uint64_t bodyLength = 0
    ) const = 0;
    
    virtual uint64_t multipartUploadSize() const = 0;//不同 Provider 对分片的支持不同
    
    virtual Instance getInstanceDetails(myblob::network::TaskedSendReceiverHandle& sendReceiver) = 0;//每家云的实例元数据路径不同
    
    virtual void initCache(myblob::network::TaskedSendReceiverHandle& sendReceiverHandle) = 0;//缓存策略因云而异
    //address_ 和 port_ 只是给 HTTPProvider 这种"地址明确写在 URL 里"的简单场景用的。
    //对 AWS/Azure/GCP 来说，地址是根据 bucket、region、账户名等拼出来的，跟基类存储的 address_ 毫无关系。 
    virtual std::string getAddress() const = 0;
    
    virtual uint16_t getPort() const = 0;
    
    virtual CloudService getType() const { return type_; }

    // 重签名支持
    virtual bool supportsResigning() const { return false; }
    
    // 获取配置
    virtual network::Config getConfig(network::TaskedSendReceiverHandle& sendReceiver);
    
    // 静态辅助方法
    static std::string getETag(std::string_view header);
    static std::string getUploadId(std::string_view body);
    
    // 通用删除请求（用于中止多部分上传）
    virtual std::unique_ptr<utils::DataVector<uint8_t>> deleteRequestGeneric(
        const std::string& filePath,
        std::string_view uploadId) const {
        // 默认实现返回空
        return nullptr;
    }

    // 下载方法
    myblob::network::HttpResponse download(const std::string& file_path,
                                          uint64_t offset = 0,
                                          uint64_t length = 0);

    // 静态工厂方法
    static std::unique_ptr<Provider> createProvider(const RemoteInfo& info,
                                                    myblob::network::ConnectionManager& conn_mgr,
                                                    myblob::network::HttpClient& http_client);

    // 旧的工厂方法（向后兼容）
    static std::unique_ptr<Provider> makeProvider(
        const std::string& url,
        bool https = false,
        const std::string& access_key = "",
        const std::string& secret_key = "",
        void* send_receiver_handle = nullptr
    );

    // 辅助方法
    static bool isRemoteFile(const std::string& url);
    static CloudService getCloudService(const std::string& url);
    static std::string getCloudServiceName(CloudService service);
    static std::string getCloudServiceName(const std::string& url);
    
    virtual bool isHTTPS() const {
        // MinIO 不在此列：由 AWS::isHTTPS() override 按端口动态判断 (443→HTTPS, 其他→HTTP)
        return type_ == CloudService::HTTPS || type_ == CloudService::AWS ||
               type_ == CloudService::Azure || type_ == CloudService::GCP ||
               type_ == CloudService::Oracle || type_ == CloudService::IBM;
    }

protected:
    myblob::network::ConnectionManager* conn_mgr_ = nullptr;
    myblob::network::HttpClient* http_client_ = nullptr;
    std::string address_;
    uint16_t port_ = 0;
    CloudService type_;

    virtual void initSecret(myblob::network::TaskedSendReceiverHandle& sendReceiverHandle) = 0;
    virtual void getSecret() = 0;

    friend class Transaction;
};

} // namespace cloud
} // namespace myblob

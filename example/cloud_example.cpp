#include "cloud/provider.hpp"
#include "network/connection_manager.hpp"
#include "network/http_client.hpp"
#include <iostream>

using namespace myblob;

// 从 AWS S3 下载数据并上传到 Azure Blob
bool migrateData(
    const std::string& sourceUrl,      // s3://bucket/path
    const std::string& destUrl,        // azure://container/path
    const std::string& awsKeyId,
    const std::string& awsSecret,
    const std::string& azureAccount,
    const std::string& azureKey)
{
    // 创建连接管理器和HTTP客户端
    network::ConnectionManager conn_mgr;
    network::HttpClient http_client;
    
    // 创建源 Provider (AWS)
    auto sourceProvider = cloud::Provider::makeProvider(
        sourceUrl, true, awsKeyId, awsSecret, nullptr);
    if (!sourceProvider) {
        std::cerr << "Failed to create AWS provider\n";
        return false;
    }
    
    // 创建目标 Provider (Azure)
    auto destProvider = cloud::Provider::makeProvider(
        destUrl, true, azureAccount, azureKey, nullptr);
    if (!destProvider) {
        std::cerr << "Failed to create Azure provider\n";
        return false;
    }
    
    // 构建下载请求
    auto getRequest = sourceProvider->getRequest("file.txt", {0, 0});
    
    // 构建上传请求（假设数据已下载到 objectData）
    std::string objectData = "downloaded data...";
    auto putRequest = destProvider->putRequest("file.txt", objectData);
    
    std::cout << "Migration request prepared\n";
    std::cout << "Source request: " << getRequest->size() << " bytes\n";
    std::cout << "Dest request: " << putRequest->size() << " bytes\n";
    
    return true;
}

int main() {
    bool success = migrateData(
        "s3://source-bucket/data/file.txt",
        "azure://dest-container/data/file.txt",
        "AKIAIOSFODNN7EXAMPLE",
        "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY",
        "myazureaccount",
        "base64encodedazurekey"
    );
    
    return success ? 0 : 1;
}
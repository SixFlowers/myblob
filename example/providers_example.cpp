#include "cloud/provider.hpp"
#include "network/connection_manager.hpp"
#include "network/http_client.hpp"
#include <iostream>

using namespace myblob;

int main() {
    // 创建连接管理器和HTTP客户端
    network::ConnectionManager conn_mgr;
    network::HttpClient http_client;
    
    // ========== AWS S3 ==========
    auto awsProvider = cloud::Provider::makeProvider(
        "s3://my-bucket/path/to/file.txt",
        true,  // HTTPS
        "AKIAIOSFODNN7EXAMPLE",
        "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY",
        nullptr  // sendReceiverHandle (可选)
    );
    
    if (awsProvider) {
        auto request = awsProvider->getRequest("path/to/file.txt", {0, 0});
        std::cout << "AWS request size: " << request->size() << " bytes\n";
    }
    
    // ========== Azure Blob ==========
    auto azureProvider = cloud::Provider::makeProvider(
        "azure://my-container/path/to/file.txt",
        true,
        "myaccount",
        "base64encodedkey...",
        nullptr
    );
    
    if (azureProvider) {
        auto request = azureProvider->getRequest("path/to/file.txt", {0, 0});
        std::cout << "Azure request size: " << request->size() << " bytes\n";
    }
    
    // ========== Google Cloud Storage ==========
    auto gcpProvider = cloud::Provider::makeProvider(
        "gs://my-bucket/path/to/file.txt",
        true,
        "service@project.iam.gserviceaccount.com",
        "base64encodedkey...",
        nullptr
    );
    
    if (gcpProvider) {
        auto request = gcpProvider->getRequest("path/to/file.txt", {0, 0});
        std::cout << "GCP request size: " << request->size() << " bytes\n";
    }
    
    // ========== MinIO ==========
    auto minioProvider = cloud::Provider::makeProvider(
        "minio://my-bucket/path/to/file.txt",
        true,
        "minioadmin",
        "minioadmin",
        nullptr
    );
    
    if (minioProvider) {
        auto request = minioProvider->getRequest("path/to/file.txt", {0, 0});
        std::cout << "MinIO request size: " << request->size() << " bytes\n";
    }
    
    return 0;
}
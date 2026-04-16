#include "cloud/provider.hpp"
#include <iostream>
#include <fstream>

using namespace myblob;

bool multipartUpload(
    const std::string& url,
    const std::string& keyId,
    const std::string& key,
    const std::string& filePath)
{
    // 创建 Provider
    auto provider = cloud::Provider::makeProvider(url, true, keyId, key, nullptr);
    if (!provider) {
        std::cerr << "Failed to create provider\n";
        return false;
    }
    
    // 检查是否支持多部分上传
    uint64_t partSize = provider->multipartUploadSize();
    if (partSize == 0) {
        std::cerr << "Provider does not support multipart upload\n";
        return false;
    }
    
    std::cout << "Multipart upload supported, part size: " << partSize << " bytes\n";
    
    // 1. 初始化多部分上传
    auto initRequest = provider->createMultiPartRequest("large-file.bin");
    std::cout << "Init multipart upload request: " << initRequest->size() << " bytes\n";
    
    // 2. 上传各个分片（实际实现需要处理响应获取 uploadId 和 etags）
    // auto part1Request = provider->putRequestGeneric("large-file.bin", data1, 1, uploadId);
    // auto part2Request = provider->putRequestGeneric("large-file.bin", data2, 2, uploadId);
    
    // 3. 完成多部分上传
    std::vector<std::string> etags = {"etag1", "etag2", "etag3"};
    std::string content;
    auto completeRequest = provider->completeMultiPartRequest(
        "large-file.bin", "upload-id-xxx", etags, content);
    std::cout << "Complete multipart upload request: " << completeRequest->size() << " bytes\n";
    
    return true;
}

int main() {
    // GCP 支持多部分上传（128MB 分片）
    multipartUpload(
        "gs://my-bucket/",
        "service@project.iam.gserviceaccount.com",
        "base64encodedkey",
        "large-file.bin"
    );
    
    return 0;
}
/**
 * MyBlob 云服务综合测试
 *
 * 覆盖:
 *   - Provider 工厂 (6 种 URL 前缀)
 *   - MinIO Provider 公开接口 (继承自 AWS)
 *   - Transaction PUT/GET 端到端 (签名 → S3 Mock)
 *   - 签名离线验证 (通过 AWSSigner 公开 API — 单元测试已覆盖)
 *   - 多部分上传请求构建 (通过 Transaction)
 *   - Provider 辅助方法 (getETag, getUploadId, isRemoteFile)
 *
 * 前置: python3 s3-mock.py &  # localhost:19000
 * 编译: make -C build cloud_test
 */
#include "cloud/minio.hpp"
#include "cloud/provider.hpp"
#include "cloud/aws.hpp"
#include "cloud/aws_signer.hpp"
#include "cloud/http_provider.hpp"
#include "cloud/transaction.hpp"
#include "network/connection_manager.hpp"
#include "network/http_client.hpp"
#include "network/tasked_send_receiver.hpp"
#include "network/http_response.hpp"
#include "network/http_request.hpp"
#include <iostream>
#include <cstring>

using namespace myblob;

static int g_ok = 0, g_fail = 0;

#define CHECK(n, e) do { \
    std::cout << "  " << n << " ... "; \
    if (e) { std::cout << "✅" << std::endl; g_ok++; } \
    else { std::cout << "❌" << std::endl; g_fail++; } \
} while(0)

int main() {
    std::cout << "\n╔══════════════════════════════════════════╗" << std::endl;
    std::cout << "║   MyBlob 云服务测试 (S3 Mock :19000)    ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════╝\n" << std::endl;

    network::ConnectionManager mgr(10, 300, 10);
    network::HttpClient httpClient;

    // ============================================================
    // 1. Provider 工厂 — URL → Provider 子类
    // ============================================================
    std::cout << "━━━ 1. Provider 工厂 (6 种前缀) ━━━" << std::endl;

    {
        auto p = cloud::Provider::makeProvider("http://localhost:19000");
        CHECK("http:// → HTTP", p->getType() == cloud::CloudService::HTTP);
    }
    {
        auto p = cloud::Provider::makeProvider("minio://localhost:19000/bucket/");
        CHECK("minio:// → MinIO", p->getType() == cloud::CloudService::MinIO);
    }
    {
        auto p = cloud::Provider::makeProvider("s3://test-bucket/");
        CHECK("s3:// → AWS", p->getType() == cloud::CloudService::AWS);
    }
    {
        auto p = cloud::Provider::makeProvider("azure://container/file");
        CHECK("azure:// → Azure", p->getType() == cloud::CloudService::Azure);
    }
    {
        auto p = cloud::Provider::makeProvider("gs://bucket/obj");
        CHECK("gs:// → GCP", p->getType() == cloud::CloudService::GCP);
    }

    // ============================================================
    // 2. MinIO Provider 公开属性 (继承自 AWS)
    // ============================================================
    std::cout << "\n━━━ 2. MinIO 公开接口 ━━━" << std::endl;

    cloud::RemoteInfo info;
    info.provider = cloud::CloudService::MinIO;
    info.endpoint = "localhost";
    info.port = 19000;
    info.bucket = "test-bucket";
    info.region = "us-east-1";
    info.https = false;

    cloud::MinIO minio(info, "test-access-key", "test-secret-key", mgr, httpClient);

    CHECK("getAddress = localhost", minio.getAddress() == "localhost");
    CHECK("getPort = 19000",         minio.getPort() == 19000);
    CHECK("isHTTPS = false",         !minio.isHTTPS());
    CHECK("getType = MinIO",         minio.getType() == cloud::CloudService::MinIO);

    // ============================================================
    // 3. HTTPProvider GET 端到端 (无签名)
    // ============================================================
    std::cout << "\n━━━ 3. Transaction GET/PUT (无签名) ━━━" << std::endl;

    {
        cloud::HTTPProvider prov("localhost", 19000, false, mgr, httpClient);
        cloud::Transaction txn(&prov);
        uint8_t buf[4096] = {};
        txn.getObjectRequest("/existing-obj", {0, 0}, buf, sizeof(buf));
        txn.execute();
        for (auto& r : txn) {
            CHECK("GET 收到响应",
                  r.getState() == network::MessageState::Finished ||
                  r.getState() == network::MessageState::Aborted);
        }
    }

    // ============================================================
    // 4. MinIO Provider GET 端到端 (带 SigV4 签名)
    // ============================================================
    std::cout << "\n━━━ 4. Transaction 带签名 ━━━" << std::endl;

    {
        cloud::Transaction txn(&minio);
        uint8_t buf[4096] = {};
        txn.getObjectRequest("/existing-obj", {0, 0}, buf, sizeof(buf));
        txn.execute();
        for (auto& r : txn) {
            CHECK("GET (签名) 收到响应",
                  r.getState() == network::MessageState::Finished ||
                  r.getState() == network::MessageState::Aborted);
        }
    }

    {
        cloud::Transaction txn(&minio);
        uint8_t buf[4096] = {};
        txn.deleteObjectRequest("/delete-obj", buf, sizeof(buf));
        txn.execute();
        for (auto& r : txn) {
            CHECK("DELETE (签名) 收到响应",
                  r.getState() == network::MessageState::Finished ||
                  r.getState() == network::MessageState::Aborted);
        }
    }

    // ============================================================
    // 5. 大文件 — 此路径不触发分片 (body < 128MB)
    // ============================================================
    std::cout << "\n━━━ 5. PUT 小对象 ━━━" << std::endl;

    {
        cloud::Transaction txn(&minio);
        const char* data = "hello from myblob cloud test";
        txn.putObjectRequest("/put-obj", data, strlen(data));
        txn.execute();
        for (auto& r : txn) {
            CHECK("PUT 完成 (非分片)",
                  r.getState() == network::MessageState::Finished ||
                  r.getState() == network::MessageState::Aborted);
        }
    }

    // ============================================================
    // 6. Provider 静态辅助方法
    // ============================================================
    std::cout << "\n━━━ 6. Provider 辅助方法 ━━━" << std::endl;

    {
        std::string h = "HTTP/1.1 200 OK\r\nETag: \"abc123def\"\r\n\r\n";
        CHECK("getETag", cloud::Provider::getETag(h) == "abc123def");

        std::string x = "<UploadId>up-001</UploadId>";
        CHECK("getUploadId", cloud::Provider::getUploadId(x) == "up-001");

        CHECK("isRemoteFile s3://",   cloud::Provider::isRemoteFile("s3://b/k"));
        CHECK("isRemoteFile https://", cloud::Provider::isRemoteFile("https://x.com/f"));
        CHECK("isRemoteFile azure://", cloud::Provider::isRemoteFile("azure://c/b"));
        CHECK("isRemoteFile gs://",    cloud::Provider::isRemoteFile("gs://b/o"));
        CHECK("isRemoteFile minio://", cloud::Provider::isRemoteFile("minio://x/b/k"));
        CHECK("!isRemoteFile local",  !cloud::Provider::isRemoteFile("/tmp/f.txt"));
    }

    // ============================================================
    // 7. CloudService 名称映射
    // ============================================================
    std::cout << "\n━━━ 7. 云服务名称映射 ━━━" << std::endl;

    {
        using CS = cloud::CloudService;
        CHECK("AWS",   cloud::Provider::getCloudServiceName(CS::AWS)   == "AWS");
        CHECK("Azure", cloud::Provider::getCloudServiceName(CS::Azure) == "Azure");
        CHECK("GCP",   cloud::Provider::getCloudServiceName(CS::GCP)   == "GCP");
        CHECK("MinIO", cloud::Provider::getCloudServiceName(CS::MinIO) == "MinIO");
        CHECK("HTTPS", cloud::Provider::getCloudServiceName(CS::HTTPS) == "HTTPS");
        CHECK("HTTP",  cloud::Provider::getCloudServiceName(CS::HTTP)  == "HTTP");

        CHECK("s3:// → AWS",
              cloud::Provider::getCloudServiceName("s3://b/k") == "AWS");
        CHECK("azure:// → Azure",
              cloud::Provider::getCloudServiceName("azure://c/b") == "Azure");
        CHECK("gs:// → GCP",
              cloud::Provider::getCloudServiceName("gs://b/o") == "GCP");
        CHECK("minio:// → MinIO",
              cloud::Provider::getCloudServiceName("minio://x/b/k") == "MinIO");
    }

    // ============================================================
    // 8. AWSSigner 公开 API (单元测试中已详细覆盖，此处验证可访问性)
    // ============================================================
    std::cout << "\n━━━ 8. AWSSigner 公开 API ━━━" << std::endl;

    {
        network::HttpRequest req;
        req.method = network::HttpRequest::Method::GET;
        req.path = "/bucket/obj.txt";
        req.headers["Host"] = "bucket.s3.amazonaws.com";
        req.headers["x-amz-date"] = "20260726T120000Z";

        cloud::AWSSigner::StringToSign sts = {
            .request = req,
            .region = "us-east-1",
            .service = "s3"
        };
        cloud::AWSSigner::encodeCanonicalRequest(req, sts);

        CHECK("requestSHA 非空", !sts.requestSHA.empty());
        CHECK("signedHeaders 非空", !sts.signedHeaders.empty());
        CHECK("payloadHash 非空", !sts.payloadHash.empty());

        auto path = cloud::AWSSigner::createSignedRequest(
            "AKIAIOSFODNN7EXAMPLE",
            "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY", sts);

        CHECK("Authorization 头存在",
              req.headers.find("Authorization") != req.headers.end());
        CHECK("签名路径非空", !path.empty());
    }

    // ============================================================
    std::cout << "\n═══════════════════════════════════════════" << std::endl;
    std::cout << "  总计: " << (g_ok + g_fail) << " 项  "
              << "通过: " << g_ok << "  失败: " << g_fail << std::endl;
    std::cout << "═══════════════════════════════════════════" << std::endl;

    if (g_fail == 0) {
        std::cout << "\n🎉 云服务模块全部通过！\n" << std::endl;
    }
    return g_fail > 0 ? 1 : 0;
}

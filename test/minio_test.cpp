/**
 * MinIO 真实端到端测试
 *
 * 前置: /tmp/minio server /tmp/minio-data --address ":19001"
 * 编译: make -C build minio_test
 */
#include "cloud/minio.hpp"
#include "cloud/provider.hpp"
#include "cloud/transaction.hpp"
#include "network/connection_manager.hpp"
#include "network/http_client.hpp"
#include "network/http_response.hpp"
#include "network/https_message.hpp"
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
    std::cout << "║   MinIO 端到端测试 (localhost:19001)     ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════╝\n" << std::endl;

    network::ConnectionManager mgr(10, 300, 10);
    network::HttpClient httpClient;

    cloud::RemoteInfo info;
    info.provider = cloud::CloudService::MinIO;
    info.endpoint = "localhost";
    info.port = 19001;
    info.bucket = "test-bucket";
    info.region = "us-east-1";
    info.https = false;

    cloud::MinIO minio(info, "minioadmin", "minioadmin", mgr, httpClient);

    CHECK("getAddress = localhost", minio.getAddress() == "localhost");
    CHECK("getPort = 19001", minio.getPort() == 19001);

    // ========== PUT 对象 ==========
    std::cout << "\n━━━ PUT 对象 ━━━" << std::endl;
    {
        cloud::Transaction txn(&minio);
        const char* data = "hello from myblob real minio test!";
        txn.putObjectRequest("/test-bucket/myblob-test.txt", data, strlen(data));
        txn.execute();

        for (auto& r : txn) {
            CHECK("PUT 200 OK", r.success());
            if (!r.success()) {
                std::cout << "    失败详情: 错误码=" << r.getFailureCode()
                          << " HTTP=" << r.getResponseCodeNumber() << std::endl;
                if (r.getSize() > 0) {
                    std::cout << "    响应: " << r.getErrorResponse() << std::endl;
                }
            }
        }
    }

    // ========== GET 对象 ==========
    std::cout << "\n━━━ GET 对象 ━━━" << std::endl;
    {
        cloud::Transaction txn(&minio);
        uint8_t buf[4096] = {};
        txn.getObjectRequest("/test-bucket/myblob-test.txt", {0, 0}, buf, sizeof(buf));
        txn.execute();

        for (auto& r : txn) {
            CHECK("GET 200 OK", r.success());
            if (r.success()) {
                std::string body = std::string(r.getResult());
                CHECK("body = hello from myblob...",
                      body.find("hello from myblob") != std::string::npos);
                std::cout << "    内容: " << body << std::endl;
            } else {
                std::cout << "    失败: 错误码=" << r.getFailureCode()
                          << " HTTP=" << r.getResponseCodeNumber() << std::endl;
                if (r.getSize() > 0)
                    std::cout << "    响应: " << r.getResult() << std::endl;
            }
        }
    }

    // ========== DELETE 对象 ==========
    std::cout << "\n━━━ DELETE 对象 ━━━" << std::endl;
    {
        cloud::Transaction txn(&minio);
        uint8_t buf[4096] = {};
        txn.deleteObjectRequest("/test-bucket/myblob-test.txt", buf, sizeof(buf));
        txn.execute();

        for (auto& r : txn) {
            CHECK("DELETE 204 No Content", r.success());
            if (!r.success()) {
                std::cout << "    失败: 错误码=" << r.getFailureCode()
                          << " HTTP=" << r.getResponseCodeNumber() << std::endl;
            }
        }
    }

    // ========== GET 已删除对象 → 404 ==========
    std::cout << "\n━━━ GET 已删除对象 → 404 ━━━" << std::endl;
    {
        cloud::Transaction txn(&minio);
        uint8_t buf[4096] = {};
        txn.getObjectRequest("/test-bucket/myblob-test.txt", {0, 0}, buf, sizeof(buf));
        txn.execute();

        for (auto& r : txn) {
            CHECK("GET 已删除返回失败", !r.success());
            CHECK("HTTP 404", r.getResponseCodeNumber() == 404);
        }
    }

    std::cout << "\n═══════════════════════════════════════════" << std::endl;
    std::cout << "  总计: " << (g_ok + g_fail) << " 项  "
              << "通过: " << g_ok << "  失败: " << g_fail << std::endl;
    std::cout << "═══════════════════════════════════════════" << std::endl;

    if (g_fail == 0)
        std::cout << "\n🎉 MinIO 端到端全部通过！" << std::endl;
    return g_fail > 0 ? 1 : 0;
}

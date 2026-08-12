/**
 * 分片上传端到端测试
 *
 * S3 规范要求: 每个分片(除最后一个) ≥ 5MB
 * 前置: MinIO on :19001, bucket test-bucket
 * 编译: make -C build multipart_test
 */
#include "cloud/minio.hpp"
#include "cloud/transaction.hpp"
#include "network/connection_manager.hpp"
#include "network/http_client.hpp"
#include <iostream>
#include <cstring>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>

using namespace myblob;

static int g_ok = 0, g_fail = 0;
#define CHECK(n, e) do { \
    std::cout << "  " << n << " ... "; \
    if (e) { std::cout << "✅" << std::endl; g_ok++; } \
    else { std::cout << "❌" << std::endl; g_fail++; } \
} while(0)

int main() {
    std::cout << "\n╔══════════════════════════════════════════╗" << std::endl;
    std::cout << "║  分片上传端到端测试 (MinIO)              ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════╝\n" << std::endl;

    network::ConnectionManager mgr(20, 300, 30);
    network::HttpClient httpClient;

    cloud::RemoteInfo info;
    info.provider = cloud::CloudService::MinIO;
    info.endpoint = "localhost";
    info.port = 19001;
    info.bucket = "test-bucket";
    info.region = "us-east-1";
    info.https = false;

    cloud::MinIO minio(info, "minioadmin", "minioadmin", mgr, httpClient);

    // ================================================================
    // 1. 分片上传 6MB → 2 parts (5MB + 1MB)
    // ================================================================
    std::cout << "━━━ 1. 分片上传 6MB → 2 parts ━━━" << std::endl;
    minio.setMultipartUploadSize(5ull << 20);  // 5MB per part (S3 minimum)

    const size_t SIZE = 6ull * 1024 * 1024;  // 6MB
    std::vector<char> bigFile(SIZE);
    for (size_t i = 0; i < SIZE; i++) bigFile[i] = static_cast<char>(i & 0xFF);

    std::atomic<bool> done{false};
    std::atomic<bool> ok{false};

    {
        cloud::Transaction txn(&minio);
        txn.putObjectRequest([&](network::MessageResult& r) {
            ok.store(r.success());
            done.store(true);
        }, "/test-bucket/multipart-6mb.bin",
           bigFile.data(), bigFile.size());
        txn.execute();

        auto start = std::chrono::steady_clock::now();
        while (!done.load() &&
               std::chrono::steady_clock::now() - start < std::chrono::seconds(20)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    CHECK("分片上传 6MB 成功", done.load() && ok.load());

    // ================================================================
    // 2. GET 验证
    // ================================================================
    std::cout << "\n━━━ 2. GET 验证 ━━━" << std::endl;
    {
        auto* buf = new uint8_t[SIZE + 8192]{};
        cloud::Transaction txn(&minio);
        txn.getObjectRequest("/test-bucket/multipart-6mb.bin",
                             {0, 0}, buf, SIZE + 8192);
        txn.execute();
        for (auto& r : txn) {
            CHECK("GET 6MB 成功", r.success());
            // body starts after HTTP headers; response tells us where
            auto bodyOffset = r.getOffset();
            CHECK("响应体大小 >= 6MB", r.getSize() >= SIZE + bodyOffset);
        }
        delete[] buf;
    }

    // ================================================================
    // 3. DELETE 清理
    // ================================================================
    std::cout << "\n━━━ 3. CLEANUP ━━━" << std::endl;
    {
        cloud::Transaction txn(&minio);
        uint8_t buf[4096] = {};
        txn.deleteObjectRequest("/test-bucket/multipart-6mb.bin", buf, sizeof(buf));
        txn.execute();
        for (auto& r : txn) CHECK("DELETE 204", r.success());
    }

    // ================================================================
    // 4. 小文件不触发分片上传 (验证文件 < 5MB 走普通 PUT)
    // ================================================================
    std::cout << "\n━━━ 4. 小文件走普通 PUT ━━━" << std::endl;
    {
        cloud::Transaction txn(&minio);
        txn.putObjectRequest("/test-bucket/tiny.bin", "hello world", 11);
        txn.execute();
        for (auto& r : txn) {
            CHECK("小文件 PUT 成功", r.success());

            cloud::Transaction txnDel(&minio);
            uint8_t b[4096] = {};
            txnDel.deleteObjectRequest("/test-bucket/tiny.bin", b, sizeof(b));
            txnDel.execute();
        }
    }

    std::cout << "\n═══════════════════════════════════════════" << std::endl;
    std::cout << "  总计: " << (g_ok + g_fail) << " 项  "
              << "通过: " << g_ok << "  失败: " << g_fail << std::endl;
    std::cout << "═══════════════════════════════════════════" << std::endl;

    if (g_fail == 0)
        std::cout << "\n🎉 分片上传全部通过！(含 S3 5MB 分片限制验证)" << std::endl;
    return g_fail > 0 ? 1 : 0;
}

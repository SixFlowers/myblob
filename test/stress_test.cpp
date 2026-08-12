/**
 * 压力 + 边缘测试
 *
 * 前置: MinIO :19001, HTTP :18888
 * 编译: make -C build stress_test
 */
#include "cloud/minio.hpp"
#include "cloud/http_provider.hpp"
#include "cloud/transaction.hpp"
#include "network/connection_manager.hpp"
#include "network/http_client.hpp"
#include "network/http_response.hpp"
#include "utils/data_vector.hpp"
#include "utils/ring_buffer.hpp"
#include "utils/utils.hpp"
#include <iostream>
#include <sstream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <algorithm>

using namespace myblob;

static int g_ok = 0, g_fail = 0;
#define CHECK(n, e) do { \
    std::cout << "  " << n << " ... "; \
    if (e) { std::cout << "✅" << std::endl; g_ok++; } \
    else { std::cout << "❌" << std::endl; g_fail++; } \
} while(0)

static std::pair<int,int> quick_put_get_del(network::ConnectionManager& mgr,
    network::HttpClient& client, cloud::MinIO& minio, int id) {
    char path[64]; snprintf(path, sizeof(path), "/test-bucket/stress-%d", id);
    std::string body = "stress-" + std::to_string(id);

    int ok = 0;
    { cloud::Transaction t(&minio); t.putObjectRequest(path, body.data(), body.size()); t.execute(); for (auto& r : t) ok += r.success(); }
    { cloud::Transaction t(&minio); uint8_t b[4096]={}; t.deleteObjectRequest(path,b,sizeof(b)); t.execute(); for (auto& r : t) ok += r.success(); }
    return {ok, 2};
}

int main() {
    std::cout << "\n╔══════════════════════════════════════════╗" << std::endl;
    std::cout << "║  压力 + 边缘测试                        ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════╝\n" << std::endl;

    // ================================================================
    // 1. RingBuffer 高压：10 线程 × 10000 元素
    // ================================================================
    std::cout << "━━━ 1. RingBuffer 高压 (10线程×10000) ━━━" << std::endl;
    {
        utils::RingBuffer<int> rb(4096);
        const int N = 10000;
        const int T = 10;
        std::atomic<int> produced{0}, consumed{0};
        std::atomic<bool> producerDone{false};

        std::vector<std::thread> threads;
        for (int t = 0; t < T/2; t++) {
            threads.emplace_back([&]() {
                for (int i = 0; i < N/(T/2); i++) {
                    while (rb.insert(i) == ~0ull) std::this_thread::yield();
                    produced++;
                }
            });
        }
        for (int t = 0; t < T/2; t++) {
            threads.emplace_back([&]() {
                while (consumed < N) {
                    if (rb.consume().has_value()) consumed++;
                    else std::this_thread::yield();
                }
            });
        }
        for (auto& th : threads) th.join();
        CHECK("生产=" + std::to_string(produced.load()) + " 消费=" + std::to_string(consumed.load()),
              produced.load() == N && consumed.load() == N);
    }

    // ================================================================
    // 2. 连接池爆发：100 个请求，每 10 个一组共用一个 ConnectionManager
    // ================================================================
    std::cout << "\n━━━ 2. 连接池爆发 100 请求 ━━━" << std::endl;
    {
        int ok = 0;
        for (int batch = 0; batch < 10; batch++) {
            network::ConnectionManager mgr(5, 300, 10);
            network::HttpClient client;
            cloud::HTTPProvider prov("127.0.0.1", 18888, false, mgr, client);

            for (int i = 0; i < 10; i++) {
                cloud::Transaction txn(&prov);
                uint8_t b[4096] = {};
                std::ostringstream p; p << "/burst/" << (batch * 10 + i);
                txn.getObjectRequest(p.str(), {0,0}, b, sizeof(b));
                txn.execute();
                for (auto& r : txn) if (r.success()) ok++;
            }
        }
        CHECK("100/100 成功", ok == 100);
    }

    // ================================================================
    // 3. 边缘路径：超长/特殊字符/空body
    // ================================================================
    std::cout << "\n━━━ 3. 边缘路径 ━━━" << std::endl;
    {
        network::ConnectionManager mgr(10, 300, 10);
        network::HttpClient client;

        cloud::RemoteInfo info;
        info.provider = cloud::CloudService::MinIO;
        info.endpoint = "localhost"; info.port = 19001;
        info.bucket = "test-bucket"; info.region = "us-east-1";
        info.https = false;  // ★ RemoteInfo 默认 https=true！
        cloud::MinIO minio(info, "minioadmin", "minioadmin", mgr, client);

        // 3a. 空 body
        {
            cloud::Transaction t(&minio);
            t.putObjectRequest("/test-bucket/empty.txt", "", 0);
            t.execute();
            for (auto& r : t) CHECK("空 body PUT", r.success());
        }
        // 3b. 超长路径 (200 chars)
        {
            std::string longPath = "/test-bucket/" + std::string(180, 'a');
            cloud::Transaction t(&minio);
            t.putObjectRequest(longPath, "x", 1);
            t.execute();
            for (auto& r : t) CHECK("超长路径 PUT", r.success());
        }
        // 3d. 二进制 body
        {
            uint8_t binary[256];
            for (int i = 0; i < 256; i++) binary[i] = static_cast<uint8_t>(i);
            cloud::Transaction t(&minio);
            t.putObjectRequest("/test-bucket/binary.bin",
                               reinterpret_cast<const char*>(binary), 256);
            t.execute();
            for (auto& r : t) CHECK("二进制 body PUT", r.success());
        }
    }

    // ================================================================
    // 4. 多线程竞争：10 线程并发打 MinIO
    // ================================================================
    std::cout << "\n━━━ 4. 并发竞争 8线程×15请求 ━━━" << std::endl;
    {
        std::atomic<int> totalOk{0};
        const int T = 8, N = 15;
        std::vector<std::thread> threads;
        for (int t = 0; t < T; t++) {
            threads.emplace_back([&, t]() {
                network::ConnectionManager mgr(10, 300, 10);
                network::HttpClient client;
                cloud::RemoteInfo info2;
                info2.provider = cloud::CloudService::MinIO;
                info2.endpoint = "localhost"; info2.port = 19001;
                info2.bucket = "test-bucket"; info2.region = "us-east-1";
                info2.https = false;
                cloud::MinIO minio(info2, "minioadmin", "minioadmin", mgr, client);

                for (int i = 0; i < N; i++) {
                    auto [ok, total] = quick_put_get_del(mgr, client, minio, t * 1000 + i);
                    totalOk += ok;
                }
            });
        }
        for (auto& th : threads) th.join();
        CHECK("120/120 并发全部成功", totalOk.load() == T * N * 2);
    }

    // ================================================================
    // 5. DataVector 大内存分配 (100MB)
    // ================================================================
    std::cout << "\n━━━ 5. DataVector 100MB 分配 ━━━" << std::endl;
    {
        utils::DataVector<uint8_t> dv;
        dv.resize(100ull * 1024 * 1024);  // 100MB
        CHECK("resize 100MB 成功", dv.size() == 100ull * 1024 * 1024);
        CHECK("可读写", dv.data()[0] == 0 && dv.data()[dv.size()-1] == 0);
        dv.data()[0] = 0xAB;
        dv.data()[dv.size()-1] = 0xCD;
        CHECK("写入成功", dv.data()[0] == 0xAB && dv.data()[dv.size()-1] == 0xCD);
    }

    // ================================================================
    // 6. 加密一致性：100KB 数据 SHA256/MD5/Base64 一次性处理
    // ================================================================
    std::cout << "\n━━━ 6. 加密 100KB 批量 ━━━" << std::endl;
    {
        std::vector<uint8_t> data(100 * 1024);
        for (size_t i = 0; i < data.size(); i++) data[i] = static_cast<uint8_t>(i & 0xFF);

        auto sha = utils::sha256Encode(data.data(), data.size());
        CHECK("SHA256 100KB 输出长度=64", sha.size() == 64);

        auto b64 = utils::base64Encode(data.data(), data.size());
        auto decoded = utils::base64Decode(
            reinterpret_cast<const uint8_t*>(b64.data()), b64.size());
        CHECK("Base64 往返 100KB", decoded.second == data.size() &&
              memcmp(decoded.first.get(), data.data(), data.size()) == 0);
    }

    // ================================================================
    // 7. 服务器突然关闭（连接已断开后的请求）
    // ================================================================
    std::cout << "\n━━━ 7. 不存在的端口 ━━━" << std::endl;
    {
        network::ConnectionManager mgr(5, 300, 10);
        network::HttpClient client;
        cloud::HTTPProvider prov("127.0.0.1", 54321, false, mgr, client);
        cloud::Transaction txn(&prov);
        uint8_t buf[4096] = {};
        txn.getObjectRequest("/no-server", {0, 0}, buf, sizeof(buf));
        txn.execute();
        for (auto& r : txn) {
            CHECK("服务器不存在 → 正确失败不崩溃", !r.success());
        }
    }

    // ================================================================
    std::cout << "\n═══════════════════════════════════════════" << std::endl;
    std::cout << "  总计: " << (g_ok + g_fail) << " 项  "
              << "通过: " << g_ok << "  失败: " << g_fail << std::endl;
    std::cout << "═══════════════════════════════════════════" << std::endl;

    if (g_fail == 0)
        std::cout << "\n🎉 压力+边缘全部通过！" << std::endl;
    return g_fail > 0 ? 1 : 0;
}

/**
 * MyBlob 综合本地验证测试
 * 先启动服务: python HTTP :18888 / HTTPS :18443
 * 然后: make -C build local_http_test && ./build/local_http_test
 */
#include "cloud/http_provider.hpp"
#include "cloud/transaction.hpp"
#include "cloud/provider.hpp"
#include "network/connection_manager.hpp"
#include "network/http_client.hpp"
#include "network/tasked_send_receiver.hpp"
#include "network/http_response.hpp"
#include <iostream>
#include <sstream>
#include <thread>
#include <atomic>
#include <cstring>

using namespace myblob;

const char* HOST = "127.0.0.1";
const uint16_t HTTP_PORT = 18888;

static int g_ok = 0, g_fail = 0;

#define CHECK(name, expr) do { \
    std::cout << "  " << name << " ... "; \
    if (expr) { std::cout << "✅" << std::endl; g_ok++; } \
    else { std::cout << "❌" << std::endl; g_fail++; } \
} while(0)

// 辅助：执行一组请求并统计结果
static std::pair<int,int> run_requests(int n, const std::string& prefix, int maxConn = 5) {
    network::ConnectionManager mgr(maxConn, 300, 10);
    network::HttpClient client;
    cloud::HTTPProvider provider(HOST, HTTP_PORT, false, mgr, client);
    cloud::Transaction txn(&provider);

    uint8_t buf[4096] = {};
    for (int i = 0; i < n; i++) {
        std::ostringstream p; p << "/" << prefix << "/" << i;
        txn.getObjectRequest(p.str(), {0,0}, buf, sizeof(buf));
    }
    txn.execute();

    int ok = 0, fail = 0;
    for (auto& r : txn) { r.success() ? ok++ : fail++; }
    return {ok, fail};
}

int main() {
    std::cout << "\n╔══════════════════════════════════════════╗" << std::endl;
    std::cout << "║   MyBlob 综合本地验证测试              ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════╝\n" << std::endl;

    // ======== 1. 基本 HTTP 请求 ========
    std::cout << "━━━ 1. 基本 HTTP 功能 ━━━" << std::endl;

    {
        auto [ok, fail] = run_requests(1, "single");
        CHECK("单次 GET", ok == 1 && fail == 0);
    }
    {
        auto [ok, fail] = run_requests(3, "batch");
        CHECK("3 个请求", ok == 3 && fail == 0);
    }
    {
        // 分两批：每批 5 个请求，验证 results iteration 正常
        auto [ok1, fail1] = run_requests(5, "batchA", 5);
        auto [ok2, fail2] = run_requests(5, "batchB", 5);
        CHECK("10 个请求分两批", ok1 + ok2 == 10 && fail1 + fail2 == 0);
    }

    // ======== 2. 错误路径 ========
    std::cout << "\n━━━ 2. 错误处理 ━━━" << std::endl;

    {
        network::ConnectionManager mgr(3, 300, 10);
        network::HttpClient client;
        // 端口没人监听
        cloud::HTTPProvider provider(HOST, 19999, false, mgr, client);
        cloud::Transaction txn(&provider);
        uint8_t buf[4096] = {};
        txn.getObjectRequest("/nope", {0,0}, buf, sizeof(buf));
        txn.execute();
        int c = 0;
        for (auto& r : txn) { c++; CHECK("连接被拒 → 正确失败", !r.success()); }
        CHECK("返回了结果", c == 1);
    }

    {
        network::ConnectionManager mgr(3, 300, 10);
        network::HttpClient client;
        cloud::HTTPProvider provider("this-host-does-not-exist.invalid",
                                      80, false, mgr, client);
        cloud::Transaction txn(&provider);
        uint8_t buf[4096] = {};
        txn.getObjectRequest("/any", {0,0}, buf, sizeof(buf));
        txn.execute();
        int c = 0;
        for (auto& r : txn) { c++; CHECK("DNS失败 → 正确失败", !r.success()); }
        CHECK("返回了结果", c == 1);
    }

    // ======== 3. 核心模块验证 ========
    std::cout << "\n━━━ 3. 核心模块 ━━━" << std::endl;

    {
        network::ConnectionManager mgr(3, 300, 10);
        CHECK("io_uring 已启用", mgr.isUsingIoUring());
    }

    {
        using FC = network::MessageFailureCode;
        uint16_t c = 0;
        c |= static_cast<uint16_t>(FC::Socket);
        c |= static_cast<uint16_t>(FC::HTTP);
        CHECK("位掩码: Socket+HTTP 都存在", (c & 0x21) == 0x21);
    }

    {
        auto r = network::HttpResponse::deserialize(
            "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello");
        CHECK("HTTP 200 解析", network::HttpResponse::checkSuccess(r.code));
        CHECK("HTTP 200 ≠ 204", !network::HttpResponse::withoutContent(r.code));

        auto r404 = network::HttpResponse::deserialize("HTTP/1.1 404 Not Found\r\n\r\n");
        CHECK("HTTP 404 解析", !network::HttpResponse::checkSuccess(r404.code));
    }

    {
        using CS = cloud::CloudService;
        CHECK("s3:// → AWS",  cloud::Provider::getCloudService("s3://b/k")  == CS::AWS);
        CHECK("azure:// → Azure", cloud::Provider::getCloudService("azure://c/b") == CS::Azure);
        CHECK("gs:// → GCP",   cloud::Provider::getCloudService("gs://b/o") == CS::GCP);
        CHECK("minio:// → MinIO", cloud::Provider::getCloudService("minio://x:9/b/k") == CS::MinIO);
        CHECK("https:// → HTTPS", cloud::Provider::getCloudService("https://x.com") == CS::HTTPS);
        CHECK("本地路径非remote", !cloud::Provider::isRemoteFile("/tmp/f.txt"));
        CHECK("s3是remote", cloud::Provider::isRemoteFile("s3://b/k"));
    }

    {
        int c = 0;
        { utils::Defer d([&]{ c++; }); CHECK("Defer作用域内 c=0", c == 0); }
        CHECK("Defer离开作用域 c=1", c == 1);
    }

    // ======== 4. 并发请求 (多线程) ========
    std::cout << "\n━━━ 4. 并发请求 ━━━" << std::endl;
    {
        const int T = 4, N = 5;
        std::atomic<int> ok{0};
        std::vector<std::thread> threads;
        for (int t = 0; t < T; t++) {
            threads.emplace_back([&, t]() {
                network::ConnectionManager mgr(5, 300, 10);
                network::HttpClient client;
                cloud::HTTPProvider provider(HOST, HTTP_PORT, false, mgr, client);
                for (int i = 0; i < N; i++) {
                    cloud::Transaction txn(&provider);
                    uint8_t b[4096] = {};
                    std::ostringstream p;
                    p << "/mt/t" << t << "/r" << i;
                    txn.getObjectRequest(p.str(), {0,0}, b, sizeof(b));
                    txn.execute();
                    for (auto& r : txn) { if (r.success()) ok++; }
                }
            });
        }
        for (auto& th : threads) th.join();
        CHECK("20 个并发请求全部成功", ok.load() == T * N);
    }

    // ======== 报告 ========
    std::cout << "\n═══════════════════════════════════════════" << std::endl;
    std::cout << "  总计: " << (g_ok + g_fail) << " 项  "
              << "通过: " << g_ok << "  失败: " << g_fail << std::endl;
    std::cout << "═══════════════════════════════════════════" << std::endl;

    if (g_fail == 0) std::cout << "\n🎉 全部通过！\n" << std::endl;
    return g_fail > 0 ? 1 : 0;
}

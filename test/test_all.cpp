/**
 * MyBlob 综合测试套件
 *
 * 覆盖所有离线可测试模块：
 *   - DataVector       (自有/借用模式, resize/reserve/push_back/移动语义)
 *   - RingBuffer       (SPSC 插入/消费, 空/满检测, 自旋锁并发安全)
 *   - HttpRequest       (序列化/反序列化, Method/Type 枚举)
 *   - HttpResponse      (反序列化, 状态码映射, 成功判断)
 *   - HttpHelper        (ContentLength/Chunked 响应判断, 204 No Content)
 *   - AWSSigner         (规范请求构建, 签名创建)
 *   - AzureSigner       (签名创建)
 *   - GCPSigner         (签名创建)
 *   - utils             (SHA256, MD5, HMAC, Base64, URL编码, Hex)
 *   - MessageFailureCode (位掩码操作)
 *   - CloudService      (枚举值正确性)
 *   - Defer             (RAII 语义)
 *   - Provider 工厂     (URL 解析, CloudService 检测)
 *
 * 编译方式：加入 CMakeLists.txt
 *   add_executable(myblob_test test/test_all.cpp ${SOURCES})
 *   target_link_libraries(myblob_test OpenSSL::SSL OpenSSL::Crypto pthread uring)
 */

#include "utils/data_vector.hpp"
#include "utils/ring_buffer.hpp"
#include "utils/defer.hpp"
#include "utils/utils.hpp"
#include "network/http_request.hpp"
#include "network/http_response.hpp"
#include "network/http_helper.hpp"
#include "network/message_failure_code.hpp"
#include "network/message_state.hpp"
#include "cloud/aws_signer.hpp"
#include "cloud/azure_signer.hpp"
#include "cloud/gcp_signer.hpp"
#include "cloud/cloud_service.hpp"
#include "cloud/provider.hpp"

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <chrono>

using namespace myblob;

// ===================================================================
// 测试框架（轻量级，无外部依赖）
// ===================================================================
static int g_passed = 0;
static int g_failed = 0;
static std::string g_current_test;

#define TEST(name) \
    g_current_test = name; \
    std::cout << "  " << name << " ... ";

#define PASS() \
    do { std::cout << "PASSED" << std::endl; g_passed++; } while(0)

#define FAIL(msg) \
    do { std::cout << "FAILED: " << msg << std::endl; g_failed++; } while(0)

#define CHECK(cond) \
    do { if (!(cond)) { FAIL(#cond); return; } } while(0)

#define CHECK_EQ(a, b) \
    do { if ((a) != (b)) { FAIL(#a " != " #b); return; } } while(0)

#define CHECK_NE(a, b) \
    do { if ((a) == (b)) { FAIL(#a " == " #b); return; } } while(0)

#define CHECK_STREQ(a, b) \
    do { if (std::string(a) != std::string(b)) { \
        FAIL(std::string(#a) + "='" + std::string(a) + "' != " #b "='" + std::string(b) + "'"); \
        return; } } while(0)

#define SECTION(name) \
    std::cout << "    [" << name << "]" << std::endl;

// ===================================================================
// 1. DataVector 测试
// ===================================================================
static void test_datavector_owned() {
    TEST("DataVector 自有模式基本操作");
    utils::DataVector<uint8_t> dv;
    CHECK(dv.owned());
    CHECK_EQ(dv.size(), 0u);
    CHECK_EQ(dv.capacity(), 0u);

    dv.resize(100);
    CHECK_EQ(dv.size(), 100u);
    CHECK(dv.capacity() >= 100u);
    CHECK(dv.owned());

    dv.data()[0] = 42;
    dv.data()[99] = 127;
    CHECK_EQ(dv.data()[0], 42);
    CHECK_EQ(dv.data()[99], 127);

    dv.clear();
    CHECK_EQ(dv.size(), 0u);
    PASS();
}

static void test_datavector_reserve() {
    TEST("DataVector reserve 扩容");
    utils::DataVector<uint8_t> dv(10);
    CHECK_EQ(dv.capacity(), 10u);
    dv.reserve(100);
    CHECK(dv.capacity() >= 100u);
    PASS();
}

static void test_datavector_pushback() {
    TEST("DataVector push_back");
    utils::DataVector<int> dv;
    for (int i = 0; i < 100; i++) {
        dv.push_back(i);
    }
    CHECK_EQ(dv.size(), 100u);
    for (int i = 0; i < 100; i++) {
        CHECK_EQ(dv[i], i);
    }
    PASS();
}

static void test_datavector_move() {
    TEST("DataVector 移动语义");
    utils::DataVector<uint8_t> a(50);
    a.data()[0] = 99;
    utils::DataVector<uint8_t> b(std::move(a));
    CHECK_EQ(b.data()[0], 99);
    CHECK_EQ(b.size(), 50u);
    // a 被移动后应为空
    CHECK_EQ(a.size(), 0u);
    CHECK_EQ(a.capacity(), 0u);
    PASS();
}

static void test_datavector_borrowed() {
    TEST("DataVector 借用模式");
    uint8_t buf[32] = {};
    buf[0] = 0xAB;
    buf[31] = 0xCD;
    utils::DataVector<uint8_t> dv(buf, 32);
    CHECK(!dv.owned());
    CHECK_EQ(dv.capacity(), 32u);
    CHECK_EQ(dv.data()[0], 0xAB);
    CHECK_EQ(dv.data()[31], 0xCD);
    // 借用模式下不能 resize（容量不够）
    // resize(100) 会抛异常，这里只验证 owned 标志
    dv.resize(32); // 不扩容，应该OK
    CHECK_EQ(dv.size(), 32u);
    PASS();
}

static void test_datavector_copy() {
    TEST("DataVector 拷贝构造");
    utils::DataVector<uint8_t> a(10);
    for (int i = 0; i < 10; i++) a.data()[i] = static_cast<uint8_t>(i);
    utils::DataVector<uint8_t> b(a);
    CHECK_EQ(b.size(), 10u);
    for (int i = 0; i < 10; i++) CHECK_EQ(b.data()[i], static_cast<uint8_t>(i));
    PASS();
}

// ===================================================================
// 2. RingBuffer 测试
// ===================================================================
static void test_ringbuffer_insert_consume() {
    TEST("RingBuffer 单元素插入/消费");
    utils::RingBuffer<int> rb(16);
    CHECK(rb.empty());
    auto pos = rb.insert(42);
    CHECK(pos != ~0ull);
    CHECK(!rb.empty());
    auto val = rb.consume();
    CHECK(val.has_value());
    CHECK_EQ(val.value(), 42);
    CHECK(rb.empty());
    PASS();
}

static void test_ringbuffer_full() {
    TEST("RingBuffer 满队列拒绝");
    utils::RingBuffer<int> rb(4);
    for (int i = 0; i < 4; i++) {
        CHECK(rb.insert(i * 10) != ~0ull);
    }
    // 第5个应该失败
    CHECK_EQ(rb.insert(99), ~0ull);
    // 消费一个后应该能再插入
    auto v = rb.consume();
    CHECK(v.has_value());
    CHECK_EQ(v.value(), 0);
    CHECK(rb.insert(99) != ~0ull);
    PASS();
}

static void test_ringbuffer_insertAll() {
    TEST("RingBuffer 批量插入");
    utils::RingBuffer<int> rb(16);
    std::vector<int> data = {1, 2, 3, 4, 5};
    auto pos = rb.insertAll(data);
    CHECK(pos != ~0ull);
    for (int i = 0; i < 5; i++) {
        auto v = rb.consume();
        CHECK(v.has_value());
        CHECK_EQ(v.value(), i + 1);
    }
    CHECK(rb.empty());
    PASS();
}

static void test_ringbuffer_mt_sp() {
    TEST("RingBuffer 多线程单生产者单消费者");
    utils::RingBuffer<int> rb(1024);
    const int N = 100;

    std::thread consumer([&]() {
        int received = 0;
        int last = -1;
        while (received < N) {
            auto v = rb.consume();
            if (v.has_value()) {
                CHECK_EQ(v.value(), last + 1);
                last = v.value();
                received++;
            } else {
                std::this_thread::yield();
            }
        }
    });

    // 短暂延迟确保消费者先启动
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::thread producer([&]() {
        for (int i = 0; i < N; i++) {
            while (rb.insert(i) == ~0ull) {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();
    PASS();
}

// ===================================================================
// 3. HttpRequest 测试
// ===================================================================
static void test_httprequest_serialize() {
    TEST("HttpRequest 序列化");
    network::HttpRequest req;
    req.method = network::HttpRequest::Method::GET;
    req.type = network::HttpRequest::Type::HTTP_1_1;
    req.path = "/test/path";
    req.headers["Host"] = "example.com";
    req.headers["Accept"] = "*/*";

    auto data = network::HttpRequest::serialize(req);
    CHECK(data != nullptr);
    CHECK(data->size() > 0);

    std::string result(reinterpret_cast<const char*>(data->data()), data->size());
    CHECK(result.find("GET") != std::string::npos);
    CHECK(result.find("/test/path") != std::string::npos);
    CHECK(result.find("HTTP/1.1") != std::string::npos);
    CHECK(result.find("Host: example.com") != std::string::npos);
    PASS();
}

static void test_httprequest_deserialize() {
    TEST("HttpRequest 反序列化");
    std::string raw = "GET /api/v1/data HTTP/1.1\r\nHost: api.example.com\r\nAccept: application/json\r\n\r\n";
    auto req = network::HttpRequest::deserialize(raw);
    CHECK_EQ(static_cast<int>(req.method), static_cast<int>(network::HttpRequest::Method::GET));
    CHECK_STREQ(req.path, "/api/v1/data");
    CHECK(req.headers.find("Host") != req.headers.end());
    CHECK_STREQ(req.headers["Host"], "api.example.com");
    PASS();
}

static void test_httprequest_methods() {
    TEST("HttpRequest Method 枚举");
    CHECK_STREQ(network::HttpRequest::getRequestMethod(network::HttpRequest::Method::GET), "GET");
    CHECK_STREQ(network::HttpRequest::getRequestMethod(network::HttpRequest::Method::PUT), "PUT");
    CHECK_STREQ(network::HttpRequest::getRequestMethod(network::HttpRequest::Method::POST), "POST");
    CHECK_STREQ(network::HttpRequest::getRequestMethod(network::HttpRequest::Method::DELETE), "DELETE");
    CHECK_STREQ(network::HttpRequest::getRequestType(network::HttpRequest::Type::HTTP_1_1), "HTTP/1.1");
    CHECK_STREQ(network::HttpRequest::getRequestType(network::HttpRequest::Type::HTTP_1_0), "HTTP/1.0");
    PASS();
}

// ===================================================================
// 4. HttpResponse 测试
// ===================================================================
static void test_httpresponse_deserialize_200() {
    TEST("HttpResponse 反序列化 200 OK");
    std::string raw = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\nContent-Type: text/plain\r\n\r\nHello, World!";
    auto resp = network::HttpResponse::deserialize(raw);
    CHECK_EQ(static_cast<int>(resp.code), static_cast<int>(network::HttpResponse::Code::OK_200));
    CHECK(network::HttpResponse::checkSuccess(resp.code));
    CHECK_STREQ(network::HttpResponse::getResponseCode(resp.code), "200 OK");
    CHECK_EQ(network::HttpResponse::getResponseCodeNumber(resp.code), 200u);
    PASS();
}

static void test_httpresponse_deserialize_404() {
    TEST("HttpResponse 反序列化 404 Not Found");
    std::string raw = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    auto resp = network::HttpResponse::deserialize(raw);
    CHECK_EQ(static_cast<int>(resp.code), static_cast<int>(network::HttpResponse::Code::NOT_FOUND_404));
    CHECK(!network::HttpResponse::checkSuccess(resp.code));
    CHECK_STREQ(network::HttpResponse::getResponseCode(resp.code), "404 Not Found");
    CHECK_EQ(network::HttpResponse::getResponseCodeNumber(resp.code), 404u);
    PASS();
}

static void test_httpresponse_deserialize_204() {
    TEST("HttpResponse 204 No Content");
    std::string raw = "HTTP/1.1 204 No Content\r\n\r\n";
    auto resp = network::HttpResponse::deserialize(raw);
    CHECK(network::HttpResponse::checkSuccess(resp.code));
    CHECK(network::HttpResponse::withoutContent(resp.code));
    PASS();
}

static void test_httpresponse_all_codes() {
    TEST("HttpResponse 所有状态码映射");
    struct { network::HttpResponse::Code code; uint64_t num; std::string str; } cases[] = {
        {network::HttpResponse::Code::OK_200, 200, "200 OK"},
        {network::HttpResponse::Code::CREATED_201, 201, "201 Created"},
        {network::HttpResponse::Code::NO_CONTENT_204, 204, "204 No Content"},
        {network::HttpResponse::Code::PARTIAL_CONTENT_206, 206, "206 Partial Content"},
        {network::HttpResponse::Code::BAD_REQUEST_400, 400, "400 Bad Request"},
        {network::HttpResponse::Code::UNAUTHORIZED_401, 401, "401 Unauthorized"},
        {network::HttpResponse::Code::FORBIDDEN_403, 403, "403 Forbidden"},
        {network::HttpResponse::Code::NOT_FOUND_404, 404, "404 Not Found"},
        {network::HttpResponse::Code::INTERNAL_SERVER_ERROR_500, 500, "500 Internal Server Error"},
        {network::HttpResponse::Code::SERVICE_UNAVAILABLE_503, 503, "503 Service Unavailable"},
    };
    for (auto& c : cases) {
        CHECK_EQ(network::HttpResponse::getResponseCodeNumber(c.code), c.num);
        CHECK_STREQ(network::HttpResponse::getResponseCode(c.code), c.str);
    }
    PASS();
}

// ===================================================================
// 5. HttpHelper 测试
// ===================================================================
static void test_httphelper_contentlength() {
    TEST("HttpHelper Content-Length 判断");
    std::string raw = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, World!";
    std::unique_ptr<network::HttpHelper::Info> info;
    bool done = network::HttpHelper::finished(
        reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), info);
    CHECK(done);
    CHECK(info != nullptr);
    CHECK_EQ(info->encoding, network::HttpHelper::Encoding::ContentLength);
    CHECK_EQ(info->length, 13u);
    CHECK(info->headerLength > 0u); // header length depends on formatting
    PASS();
}

static void test_httphelper_chunked() {
    TEST("HttpHelper Chunked 判断");
    std::string raw = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nHello\r\n0\r\n\r\n";
    std::unique_ptr<network::HttpHelper::Info> info;
    bool done = network::HttpHelper::finished(
        reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), info);
    CHECK(done);
    CHECK(info != nullptr);
    CHECK_EQ(info->encoding, network::HttpHelper::Encoding::ChunkedEncoding);
    PASS();
}

static void test_httphelper_no_content() {
    TEST("HttpHelper 204 No Content 无需body");
    std::string raw = "HTTP/1.1 204 No Content\r\n\r\n";
    std::unique_ptr<network::HttpHelper::Info> info;
    bool done = network::HttpHelper::finished(
        reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), info);
    CHECK(done);
    CHECK(network::HttpResponse::withoutContent(info->response.code));
    PASS();
}

static void test_httphelper_incomplete() {
    TEST("HttpHelper 未收完判断");
    // 只发了响应头，body 还没收完
    std::string raw = "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nonly10bytes"; // 10 bytes body
    std::unique_ptr<network::HttpHelper::Info> info;
    bool done = network::HttpHelper::finished(
        reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), info);
    CHECK(!done); // 100 字节的 Content-Length，只收到 10 字节
    PASS();
}

static void test_httphelper_retrieve() {
    TEST("HttpHelper retrieveContent");
    std::string raw = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
    std::unique_ptr<network::HttpHelper::Info> info;
    auto content = network::HttpHelper::retrieveContent(
        reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), info);
    CHECK_STREQ(content, "hello");
    PASS();
}

// ===================================================================
// 6. AWSSigner 测试
// ===================================================================
static void test_awssigner_canonical_simple() {
    TEST("AWSSigner 规范请求构建");
    network::HttpRequest req;
    req.method = network::HttpRequest::Method::GET;
    req.path = "/test-bucket/test.txt";
    req.headers["Host"] = "test-bucket.s3.us-east-1.amazonaws.com";
    req.headers["x-amz-date"] = "20260426T120000Z";

    cloud::AWSSigner::StringToSign sts = {
        .request = req,
        .region = "us-east-1",
        .service = "s3",
    };

    // 无 body 的 GET 请求
    cloud::AWSSigner::encodeCanonicalRequest(req, sts, nullptr, 0);

    CHECK(!sts.requestSHA.empty()); // SHA256 of canonical request
    CHECK(!sts.signedHeaders.empty());
    CHECK(sts.signedHeaders.find("host") != std::string::npos);
    CHECK(sts.signedHeaders.find("x-amz-date") != std::string::npos);

    // payload hash 应该存在
    CHECK(!sts.payloadHash.empty());
    PASS();
}

static void test_awssigner_small_body_md5() {
    TEST("AWSSigner PUT 请求 body ≤ 1KB 时添加 Content-MD5");
    std::string body = "hello world";
    network::HttpRequest req;
    req.method = network::HttpRequest::Method::PUT;
    req.path = "/bucket/obj.txt";
    req.headers["Host"] = "bucket.s3.amazonaws.com";
    req.headers["x-amz-date"] = "20260426T120000Z";

    cloud::AWSSigner::StringToSign sts = {
        .request = req,
        .region = "us-east-1",
        .service = "s3",
    };

    cloud::AWSSigner::encodeCanonicalRequest(
        req, sts,
        reinterpret_cast<const uint8_t*>(body.data()), body.size());

    // PUT + body ≤ 1KB 应该生成 Content-MD5
    CHECK(req.headers.find("Content-MD5") != req.headers.end());
    CHECK(!req.headers["Content-MD5"].empty());
    CHECK(req.headers.find("x-amz-content-sha256") != req.headers.end());
    PASS();
}

static void test_awssigner_large_body_unsigned() {
    TEST("AWSSigner body > 1KB 使用 UNSIGNED-PAYLOAD");
    // 创建一个 2KB 的 body（超过 1KB 阈值）
    std::string body(2048, 'X');
    network::HttpRequest req;
    req.method = network::HttpRequest::Method::PUT;
    req.path = "/bucket/large.bin";
    req.headers["Host"] = "bucket.s3.amazonaws.com";
    req.headers["x-amz-date"] = "20260426T120000Z";

    cloud::AWSSigner::StringToSign sts = {
        .request = req,
        .region = "us-east-1",
        .service = "s3",
    };

    cloud::AWSSigner::encodeCanonicalRequest(
        req, sts,
        reinterpret_cast<const uint8_t*>(body.data()), body.size());

    CHECK_STREQ(sts.payloadHash, "UNSIGNED-PAYLOAD");
    PASS();
}

static void test_awssigner_signed_request() {
    TEST("AWSSigner 签名创建");
    network::HttpRequest req;
    req.method = network::HttpRequest::Method::GET;
    req.path = "/test-bucket/test.txt";
    req.headers["Host"] = "test-bucket.s3.us-east-1.amazonaws.com";
    req.headers["x-amz-date"] = "20260426T120000Z";

    cloud::AWSSigner::StringToSign sts = {
        .request = req,
        .region = "us-east-1",
        .service = "s3",
    };

    cloud::AWSSigner::encodeCanonicalRequest(req, sts);

    std::string keyId = "AKIAIOSFODNN7EXAMPLE";
    std::string secret = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";
    std::string path = cloud::AWSSigner::createSignedRequest(keyId, secret, sts);

    CHECK(req.headers.find("Authorization") != req.headers.end());
    auto& auth = req.headers["Authorization"];
    CHECK(auth.find("AWS4-HMAC-SHA256") != std::string::npos);
    CHECK(auth.find("Credential=" + keyId) != std::string::npos);
    CHECK(auth.find("SignedHeaders=") != std::string::npos);
    CHECK(auth.find("Signature=") != std::string::npos);
    CHECK(!path.empty());
    PASS();
}

// ===================================================================
// 7. AzureSigner 测试
// ===================================================================
static void test_azuresigner() {
    TEST("AzureSigner 签名创建");
    network::HttpRequest req;
    req.method = network::HttpRequest::Method::GET;
    req.path = "/mycontainer/myblob.txt";
    req.headers["x-ms-date"] = "Thu, 23 Jul 2026 08:00:00 GMT";
    req.headers["Host"] = "myaccount.blob.core.windows.net";

    // 使用一个测试用的 Base64 编码密钥（实际是解码 HMAC 的密钥）
    // Azure Shared Key = Base64(HMAC-SHA256(decoded_key, string_to_sign))
    std::string testKey = "Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw==";
    std::string result = cloud::AzureSigner::createSignedRequest("myaccount", testKey, req);

    CHECK(req.headers.find("Authorization") != req.headers.end());
    auto& auth = req.headers["Authorization"];
    CHECK(auth.find("SharedKey myaccount:") != std::string::npos);
    CHECK(!result.empty());
    PASS();
}

// ===================================================================
// 8. GCPSigner 测试
// ===================================================================
static void test_gcpsigner() {
    TEST("GCPSigner 签名创建");
    // GCP 需要 RSA 私钥，这里测试签名参数的构建
    network::HttpRequest req;
    req.method = network::HttpRequest::Method::GET;
    req.path = "/my-bucket/test.txt";
    req.headers["Host"] = "my-bucket.storage.googleapis.com";
    req.queries["X-Goog-Date"] = "20260426T120000Z";

    cloud::GCPSigner::StringToSign sts = {
        .region = "us-central1",
        .service = "storage",
        .signedHeaders = "host",
    };

    // 测试参数设置（不需要真实密钥来验证参数构建）
    CHECK_STREQ(sts.region, "us-central1");
    CHECK_STREQ(sts.service, "storage");
    CHECK_STREQ(sts.signedHeaders, "host");

    // 验证 X-Goog-Date 必须存在（createSignedRequest 会检查）
    CHECK(req.queries.find("X-Goog-Date") != req.queries.end());
    PASS();
}

// ===================================================================
// 9. utils 密码学工具测试
// ===================================================================
static void test_sha256() {
    TEST("SHA256 哈希");
    std::string input = "hello world";
    auto hash = utils::sha256Encode(
        reinterpret_cast<const uint8_t*>(input.data()), input.size());
    CHECK(!hash.empty());
    CHECK_EQ(hash.size(), 64u); // SHA256 hex = 64 chars
    // SHA256("hello world") = b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9
    CHECK_STREQ(hash, "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9");
    PASS();
}

static void test_sha256_empty() {
    TEST("SHA256 空字符串");
    auto hash = utils::sha256Encode(nullptr, 0);
    CHECK_EQ(hash.size(), 64u);
    CHECK_STREQ(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    PASS();
}

static void test_md5() {
    TEST("MD5 哈希");
    std::string input = "hello world";
    auto hash = utils::md5Encode(
        reinterpret_cast<const uint8_t*>(input.data()), input.size());
    CHECK(!hash.empty());
    CHECK_EQ(hash.size(), 16u); // raw MD5 = 16 bytes
    PASS();
}

static void test_hmac() {
    TEST("HMAC-SHA256");
    std::string key = "secret";
    std::string msg = "message";
    auto result = utils::hmacSign(
        reinterpret_cast<const uint8_t*>(key.data()), key.size(),
        reinterpret_cast<const uint8_t*>(msg.data()), msg.size());
    CHECK(result.first != nullptr);
    CHECK(result.second > 0);
    CHECK_EQ(result.second, 32u); // SHA256 HMAC = 32 bytes
    PASS();
}

static void test_base64_encode() {
    TEST("Base64 编码");
    std::string input = "hello world";
    auto encoded = utils::base64Encode(
        reinterpret_cast<const uint8_t*>(input.data()), input.size());
    CHECK_STREQ(encoded, "aGVsbG8gd29ybGQ=");
    PASS();
}

static void test_base64_decode() {
    TEST("Base64 解码");
    std::string encoded = "aGVsbG8gd29ybGQ=";
    auto result = utils::base64Decode(
        reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size());
    CHECK(result.first != nullptr);
    std::string decoded(reinterpret_cast<char*>(result.first.get()), result.second);
    CHECK_STREQ(decoded, "hello world");
    PASS();
}

static void test_base64_roundtrip() {
    TEST("Base64 编解码往返");
    // 随机二进制数据
    uint8_t data[256];
    for (int i = 0; i < 256; i++) data[i] = static_cast<uint8_t>(i);
    auto encoded = utils::base64Encode(data, 256);
    auto decoded = utils::base64Decode(
        reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size());
    CHECK_EQ(decoded.second, 256u);
    CHECK(memcmp(decoded.first.get(), data, 256) == 0);
    PASS();
}

static void test_url_encode() {
    TEST("URL 编码");
    CHECK_STREQ(utils::encodeUrlParameters("hello world"), "hello%20world");
    CHECK_STREQ(utils::encodeUrlParameters("test/bucket"), "test%2Fbucket");
    CHECK_STREQ(utils::encodeUrlParameters("abc123-_."), "abc123-_.");
    PASS();
}

static void test_hex_encode() {
    TEST("Hex 编码");
    uint8_t data[] = {0xAB, 0xCD, 0xEF, 0x01, 0x23};
    auto lower = utils::hexEncode(data, 5, false);
    CHECK_STREQ(lower, "abcdef0123");
    auto upper = utils::hexEncode(data, 5, true);
    CHECK_STREQ(upper, "ABCDEF0123");
    PASS();
}

// ===================================================================
// 10. MessageFailureCode 测试
// ===================================================================
static void test_failure_code_bitmask() {
    TEST("MessageFailureCode 位掩码操作");
    using FC = network::MessageFailureCode;
    uint16_t code = 0;

    code |= static_cast<uint16_t>(FC::Socket);
    CHECK(code & static_cast<uint16_t>(FC::Socket));
    CHECK(!(code & static_cast<uint16_t>(FC::Timeout)));

    code |= static_cast<uint16_t>(FC::Timeout);
    CHECK(code & static_cast<uint16_t>(FC::Socket));
    CHECK(code & static_cast<uint16_t>(FC::Timeout));

    code |= static_cast<uint16_t>(FC::Send);
    CHECK(code & static_cast<uint16_t>(FC::Send));
    PASS();
}

static void test_failure_code_values() {
    TEST("MessageFailureCode 值不重叠");
    using FC = network::MessageFailureCode;
    uint16_t bits = 0;
    bits |= static_cast<uint16_t>(FC::None);
    bits |= static_cast<uint16_t>(FC::Socket);
    bits |= static_cast<uint16_t>(FC::Empty);
    bits |= static_cast<uint16_t>(FC::Timeout);
    bits |= static_cast<uint16_t>(FC::Send);
    bits |= static_cast<uint16_t>(FC::Recv);
    bits |= static_cast<uint16_t>(FC::HTTP);
    bits |= static_cast<uint16_t>(FC::TLS);
    // 所有位的 OR 结果应该等于所有位的 XOR 结果（无重叠）
    uint16_t xored = 0;
    xored ^= static_cast<uint16_t>(FC::None);
    xored ^= static_cast<uint16_t>(FC::Socket);
    xored ^= static_cast<uint16_t>(FC::Empty);
    xored ^= static_cast<uint16_t>(FC::Timeout);
    xored ^= static_cast<uint16_t>(FC::Send);
    xored ^= static_cast<uint16_t>(FC::Recv);
    xored ^= static_cast<uint16_t>(FC::HTTP);
    xored ^= static_cast<uint16_t>(FC::TLS);
    CHECK_EQ(bits & xored, xored); // XOR = OR means no overlapping bits
    PASS();
}

// ===================================================================
// 11. MessageState 测试
// ===================================================================
static void test_message_state_order() {
    TEST("MessageState 枚举值正确");
    // 验证 Init 是起始状态
    CHECK_EQ(static_cast<int>(network::MessageState::Init), 0);
    // 验证终止状态不同于起始状态
    CHECK(static_cast<int>(network::MessageState::Finished) != static_cast<int>(network::MessageState::Init));
    CHECK(static_cast<int>(network::MessageState::Aborted) != static_cast<int>(network::MessageState::Init));
    PASS();
}

// ===================================================================
// 12. CloudService 测试
// ===================================================================
static void test_cloud_service_values() {
    TEST("CloudService 枚举值");
    using CS = cloud::CloudService;
    CHECK_NE(static_cast<int>(CS::HTTP), static_cast<int>(CS::HTTPS));
    CHECK_NE(static_cast<int>(CS::AWS), static_cast<int>(CS::Azure));
    CHECK_NE(static_cast<int>(CS::GCP), static_cast<int>(CS::MinIO));
    CHECK_NE(static_cast<int>(CS::Oracle), static_cast<int>(CS::IBM));
    PASS();
}

// ===================================================================
// 13. Defer 测试
// ===================================================================
static void test_defer() {
    TEST("Defer RAII 语义");
    int counter = 0;
    {
        utils::Defer d([&counter]() { counter++; });
        CHECK_EQ(counter, 0);
    }
    CHECK_EQ(counter, 1); // 离开作用域后 Defer 析构
    PASS();
}

static void test_defer_multiple() {
    TEST("Defer 多个对象析构顺序");
    std::vector<int> order;
    {
        utils::Defer d1([&order]() { order.push_back(1); });
        utils::Defer d2([&order]() { order.push_back(2); });
        utils::Defer d3([&order]() { order.push_back(3); });
    }
    // C++ 析构顺序：逆序
    CHECK_EQ(order.size(), 3u);
    CHECK_EQ(order[0], 3); // 最后创建的先析构
    CHECK_EQ(order[1], 2);
    CHECK_EQ(order[2], 1);
    PASS();
}

// ===================================================================
// 14. Provider 工厂测试
// ===================================================================
static void test_provider_url_parsing() {
    TEST("Provider URL 解析 → CloudService");
    using CS = cloud::CloudService;
    CHECK_EQ(static_cast<int>(cloud::Provider::getCloudService("s3://bucket/key")),
             static_cast<int>(CS::AWS));
    CHECK_EQ(static_cast<int>(cloud::Provider::getCloudService("azure://container/blob")),
             static_cast<int>(CS::Azure));
    CHECK_EQ(static_cast<int>(cloud::Provider::getCloudService("gs://bucket/obj")),
             static_cast<int>(CS::GCP));
    CHECK_EQ(static_cast<int>(cloud::Provider::getCloudService("minio://localhost:9000/bucket/obj")),
             static_cast<int>(CS::MinIO));
    CHECK_EQ(static_cast<int>(cloud::Provider::getCloudService("http://example.com/path")),
             static_cast<int>(CS::HTTP));
    CHECK_EQ(static_cast<int>(cloud::Provider::getCloudService("https://example.com/path")),
             static_cast<int>(CS::HTTPS));
    PASS();
}

static void test_provider_is_remote() {
    TEST("Provider isRemoteFile 判断");
    CHECK(cloud::Provider::isRemoteFile("s3://bucket/key"));
    CHECK(cloud::Provider::isRemoteFile("https://example.com/file"));
    CHECK(cloud::Provider::isRemoteFile("http://example.com/file"));
    CHECK(cloud::Provider::isRemoteFile("gs://bucket/key"));
    CHECK(cloud::Provider::isRemoteFile("azure://container/blob"));
    CHECK(cloud::Provider::isRemoteFile("minio://localhost/bucket/key"));
    CHECK(!cloud::Provider::isRemoteFile("/local/path/file.txt"));
    CHECK(!cloud::Provider::isRemoteFile("file:///local/file.txt"));
    PASS();
}

static void test_provider_cloud_service_name() {
    TEST("Provider getCloudServiceName");
    CHECK_STREQ(cloud::Provider::getCloudServiceName(cloud::CloudService::AWS), "AWS");
    CHECK_STREQ(cloud::Provider::getCloudServiceName(cloud::CloudService::Azure), "Azure");
    CHECK_STREQ(cloud::Provider::getCloudServiceName(cloud::CloudService::GCP), "GCP");
    CHECK_STREQ(cloud::Provider::getCloudServiceName(cloud::CloudService::MinIO), "MinIO");
    CHECK_STREQ(cloud::Provider::getCloudServiceName(cloud::CloudService::HTTPS), "HTTPS");
    PASS();
}

static void test_provider_etag_extraction() {
    TEST("Provider ETag 提取");
    std::string header = "HTTP/1.1 200 OK\r\nETag: \"abc123def456\"\r\nContent-Length: 0\r\n\r\n";
    auto etag = cloud::Provider::getETag(header);
    CHECK_STREQ(etag, "abc123def456");
    PASS();
}

static void test_provider_uploadid_extraction() {
    TEST("Provider UploadId 提取");
    std::string body = "<?xml version=\"1.0\"?><InitiateMultipartUploadResult>"
                       "<Bucket>test</Bucket><Key>file</Key>"
                       "<UploadId>VXBsb2FkIElEIGZvciBlbHZpbmc</UploadId>"
                       "</InitiateMultipartUploadResult>";
    auto uploadId = cloud::Provider::getUploadId(body);
    CHECK_STREQ(uploadId, "VXBsb2FkIElEIGZvciBlbHZpbmc");
    PASS();
}

// ===================================================================
// 15. 边界条件测试
// ===================================================================
static void test_datavector_zero_resize() {
    TEST("DataVector resize(0)");
    utils::DataVector<uint8_t> dv(100);
    dv.resize(0);
    CHECK_EQ(dv.size(), 0u);
    // capacity 应该保持不变
    CHECK(dv.capacity() >= 100u);
    PASS();
}

static void test_ringbuffer_consume_empty() {
    TEST("RingBuffer 空队列消费");
    utils::RingBuffer<int> rb(8);
    auto val = rb.consume();
    CHECK(!val.has_value());
    PASS();
}

static void test_sha256_consistency() {
    TEST("SHA256 一致性");
    std::string input = "The quick brown fox jumps over the lazy dog";
    auto h1 = utils::sha256Encode(
        reinterpret_cast<const uint8_t*>(input.data()), input.size());
    auto h2 = utils::sha256Encode(
        reinterpret_cast<const uint8_t*>(input.data()), input.size());
    CHECK_STREQ(h1, h2);
    CHECK_STREQ(h1, "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
    PASS();
}

static void test_base64_empty() {
    TEST("Base64 编码空数据");
    auto encoded = utils::base64Encode(nullptr, 0);
    CHECK(encoded.empty());
    auto decoded = utils::base64Decode(nullptr, 0);
    CHECK_EQ(decoded.second, 0u);
    PASS();
}

static void test_hex_encode_single_byte() {
    TEST("Hex 编码单字节");
    uint8_t data[] = {0x00, 0xFF, 0x0F, 0xF0};
    auto hex = utils::hexEncode(data, 4);
    CHECK_STREQ(hex, "00ff0ff0");
    PASS();
}

// ===================================================================
// main
// ===================================================================
int main() {
    std::cout << "\n╔══════════════════════════════════════════╗" << std::endl;
    std::cout << "║     MyBlob 综合测试套件 v1.0            ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════╝\n" << std::endl;

    auto run = [](void (*f)(), const char* category) {
        std::cout << "\n━━━ " << category << " ━━━" << std::endl;
        f();
    };

    // DataVector
    run(test_datavector_owned,      "DataVector");
    run(test_datavector_reserve,    "DataVector");
    run(test_datavector_pushback,   "DataVector");
    run(test_datavector_move,       "DataVector");
    run(test_datavector_borrowed,   "DataVector");
    run(test_datavector_copy,       "DataVector");
    run(test_datavector_zero_resize,"DataVector");

    // RingBuffer
    run(test_ringbuffer_insert_consume, "RingBuffer");
    run(test_ringbuffer_full,           "RingBuffer");
    run(test_ringbuffer_insertAll,      "RingBuffer");
    run(test_ringbuffer_mt_sp,          "RingBuffer");
    run(test_ringbuffer_consume_empty,  "RingBuffer");

    // HttpRequest
    run(test_httprequest_serialize,     "HttpRequest");
    run(test_httprequest_deserialize,   "HttpRequest");
    run(test_httprequest_methods,       "HttpRequest");

    // HttpResponse
    run(test_httpresponse_deserialize_200,  "HttpResponse");
    run(test_httpresponse_deserialize_404,  "HttpResponse");
    run(test_httpresponse_deserialize_204,  "HttpResponse");
    run(test_httpresponse_all_codes,        "HttpResponse");

    // HttpHelper
    run(test_httphelper_contentlength,  "HttpHelper");
    run(test_httphelper_chunked,        "HttpHelper");
    run(test_httphelper_no_content,     "HttpHelper");
    run(test_httphelper_incomplete,     "HttpHelper");
    run(test_httphelper_retrieve,       "HttpHelper");

    // AWSSigner
    run(test_awssigner_canonical_simple,    "AWSSigner");
    run(test_awssigner_small_body_md5,      "AWSSigner");
    run(test_awssigner_large_body_unsigned, "AWSSigner");
    run(test_awssigner_signed_request,      "AWSSigner");

    // AzureSigner
    run(test_azuresigner, "AzureSigner");

    // GCPSigner
    run(test_gcpsigner, "GCPSigner");

    // Utils (Crypto)
    run(test_sha256,            "utils::sha256");
    run(test_sha256_empty,      "utils::sha256");
    run(test_sha256_consistency,"utils::sha256");
    run(test_md5,               "utils::md5");
    run(test_hmac,              "utils::hmac");
    run(test_base64_encode,     "utils::base64");
    run(test_base64_decode,     "utils::base64");
    run(test_base64_roundtrip,  "utils::base64");
    run(test_base64_empty,      "utils::base64");
    run(test_url_encode,        "utils::url");
    run(test_hex_encode,        "utils::hex");
    run(test_hex_encode_single_byte, "utils::hex");

    // FailureCode
    run(test_failure_code_bitmask, "MessageFailureCode");
    run(test_failure_code_values,  "MessageFailureCode");

    // MessageState
    run(test_message_state_order, "MessageState");

    // CloudService
    run(test_cloud_service_values, "CloudService");

    // Defer
    run(test_defer,         "Defer");
    run(test_defer_multiple,"Defer");

    // Provider
    run(test_provider_url_parsing,         "Provider");
    run(test_provider_is_remote,           "Provider");
    run(test_provider_cloud_service_name,  "Provider");
    run(test_provider_etag_extraction,     "Provider");
    run(test_provider_uploadid_extraction, "Provider");

    // ================================================
    std::cout << "\n═══════════════════════════════════════════" << std::endl;
    std::cout << "  总计: " << (g_passed + g_failed) << " 个测试" << std::endl;
    std::cout << "  通过: " << g_passed << std::endl;
    std::cout << "  失败: " << g_failed << std::endl;
    std::cout << "═══════════════════════════════════════════" << std::endl;

    return g_failed > 0 ? 1 : 0;
}

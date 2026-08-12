#include "utils/data_vector.hpp"
#include "utils/ring_buffer.hpp"
#include "utils/utils.hpp"
#include "utils/defer.hpp"
#include <iostream>
#include <cstring>
#include <cassert>

#define T(name) std::cout << "  " << name << " ... "; std::cout.flush();
#define P  std::cout << "PASSED" << std::endl
#define F(msg) std::cout << "FAILED: " << msg << std::endl; return 1

int main() {
    std::cout << "\n=== Quick Test ===\n" << std::endl;

    T("DataVector push_back");
    {
        myblob::utils::DataVector<int> dv;
        for (int i = 0; i < 100; i++) dv.push_back(i);
        assert(dv.size() == 100);
        for (int i = 0; i < 100; i++) assert(dv[i] == i);
        P;
    }

    T("DataVector move");
    {
        myblob::utils::DataVector<uint8_t> a(50);
        a.data()[0] = 99;
        myblob::utils::DataVector<uint8_t> b(std::move(a));
        assert(b.data()[0] == 99 && a.size() == 0);
        P;
    }

    T("DataVector borrowed");
    {
        uint8_t buf[32] = {0xAB};
        myblob::utils::DataVector<uint8_t> dv(buf, 32);
        assert(!dv.owned());
        P;
    }

    T("RingBuffer insert/consume");
    {
        myblob::utils::RingBuffer<int> rb(16);
        rb.insert(42);
        auto v = rb.consume();
        assert(v.has_value() && v.value() == 42);
        P;
    }

    T("RingBuffer full");
    {
        myblob::utils::RingBuffer<int> rb(4);
        for (int i = 0; i < 4; i++) rb.insert(i);
        assert(rb.insert(99) == ~0ull);
        rb.consume();
        assert(rb.insert(99) != ~0ull);
        P;
    }

    T("SHA256");
    {
        std::string in = "hello world";
        auto h = myblob::utils::sha256Encode(
            (const uint8_t*)in.data(), in.size());
        assert(h == "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9");
        P;
    }

    T("Base64 encode");
    {
        std::string in = "hello world";
        auto e = myblob::utils::base64Encode(
            (const uint8_t*)in.data(), in.size());
        assert(e == "aGVsbG8gd29ybGQ=");
        P;
    }

    T("Base64 roundtrip");
    {
        uint8_t data[256];
        for (int i = 0; i < 256; i++) data[i] = (uint8_t)i;
        auto e = myblob::utils::base64Encode(data, 256);
        auto d = myblob::utils::base64Decode(
            (const uint8_t*)e.data(), e.size());
        assert(d.second == 256 && memcmp(d.first.get(), data, 256) == 0);
        P;
    }

    T("URL encode");
    {
        auto e = myblob::utils::encodeUrlParameters("hello world");
        assert(e == "hello%20world");
        P;
    }

    T("Hex encode");
    {
        uint8_t d[] = {0xAB, 0xCD, 0xEF};
        auto h = myblob::utils::hexEncode(d, 3);
        assert(h == "abcdef");
        P;
    }

    T("Defer");
    {
        int c = 0;
        { myblob::utils::Defer d([&c]{ c++; }); assert(c == 0); }
        assert(c == 1);
        P;
    }

    T("HMAC");
    {
        std::string key = "secret", msg = "message";
        auto r = myblob::utils::hmacSign(
            (const uint8_t*)key.data(), key.size(),
            (const uint8_t*)msg.data(), msg.size());
        assert(r.first != nullptr && r.second == 32);
        P;
    }

    std::cout << "\nAll quick tests passed!\n" << std::endl;
    return 0;
}

#pragma once
#include "http_response.hpp"
#include <cstdint>
#include <memory>
#include <string_view>

namespace myblob::network {

class HttpHelper {
public:
    enum class Encoding : uint8_t {
        Unknown,
        ContentLength,
        ChunkedEncoding
    };
    
    struct Info {
        HttpResponse response;
        uint64_t length{0};
        uint32_t headerLength{0};
        Encoding encoding{Encoding::Unknown};
    };
    
    static std::string retrieveContent(
        const uint8_t* data,
        uint64_t length,
        std::unique_ptr<Info>& info
    );
    
    static bool finished(
        const uint8_t* data,
        uint64_t length,
        std::unique_ptr<Info>& info
    );

private:
    static Info detect(std::string_view header);
};

}  // namespace myblob::network

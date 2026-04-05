#include "network/http_helper.hpp"
#include <charconv>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace myblob::network {

HttpHelper::Info HttpHelper::detect(std::string_view header) {
    Info info;
    info.response = HttpResponse::deserialize(header);
    
    static constexpr std::string_view transferEncoding = "Transfer-Encoding";
    static constexpr std::string_view chunkedEncoding = "chunked";
    static constexpr std::string_view contentLength = "Content-Length";
    static constexpr std::string_view headerEnd = "\r\n\r\n";
    
    info.encoding = Encoding::Unknown;
    
    for (auto& keyValue : info.response.headers) {
        if (transferEncoding == keyValue.first && chunkedEncoding == keyValue.second) {
            info.encoding = Encoding::ChunkedEncoding;
            auto end = header.find(headerEnd);
            info.headerLength = static_cast<uint32_t>(end + headerEnd.length());
        } else if (contentLength == keyValue.first) {
            info.encoding = Encoding::ContentLength;
            std::from_chars(keyValue.second.data(), 
                           keyValue.second.data() + keyValue.second.size(), 
                           info.length);
            auto end = header.find(headerEnd);
            info.headerLength = static_cast<uint32_t>(end + headerEnd.length());
        }
    }
    
    if (info.encoding == Encoding::Unknown && 
        !HttpResponse::withoutContent(info.response.code)) {
        throw std::runtime_error("Unsupported HTTP encoding protocol");
    }
    
    if (HttpResponse::withoutContent(info.response.code)) {
        info.headerLength = static_cast<uint32_t>(header.length());
    }
    
    return info;
}

std::string HttpHelper::retrieveContent(
    const uint8_t* data,
    uint64_t length,
    std::unique_ptr<Info>& info
) {
    std::string_view sv(reinterpret_cast<const char*>(data), length);
    
    if (!info) {
        info = std::make_unique<Info>(detect(sv));
    }
    
    if (info->encoding == Encoding::ContentLength) {
        return std::string(reinterpret_cast<const char*>(data) + info->headerLength, 
                          info->length);
    }
    
    return {};
}

bool HttpHelper::finished(
    const uint8_t* data,
    uint64_t length,
    std::unique_ptr<Info>& info
) {
    if (!info) {
        std::string_view sv(reinterpret_cast<const char*>(data), length);
        info = std::make_unique<Info>(detect(sv));
    }
    
    if (HttpResponse::withoutContent(info->response.code)) {
        return true;
    }
    
    switch (info->encoding) {
        case Encoding::ContentLength:
            return length >= info->headerLength + info->length;
        case Encoding::ChunkedEncoding: {
            std::string_view sv(reinterpret_cast<const char*>(data), length);
            static constexpr std::string_view chunkedEnd = "0\r\n\r\n";
            auto pos = sv.find(chunkedEnd);
            bool ret = pos != sv.npos;
            if (ret) {
                info->length = pos - info->headerLength;
            }
            return ret;
        }
        default: {
            info = nullptr;
            throw std::runtime_error("Unsupported HTTP transfer protocol");
        }
    }
}

}  // namespace myblob::network

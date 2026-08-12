#include "network/http_helper.hpp"
#include <charconv>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace myblob::network {

//解析 HTTP 响应头，提取编码方式、Content-Length、响应头长度等信息
//由 finished() 和 retrieveContent() 在首次调用时触发
//参数 header 是原始的 HTTP 响应数据（包含状态行 + 响应头 + 可能的部分 body）
HttpHelper::Info HttpHelper::detect(std::string_view header) {
    Info info;
    //第1步：用 HttpResponse::deserialize() 解析状态行和响应头
    info.response = HttpResponse::deserialize(header);
    
    static constexpr std::string_view transferEncoding = "Transfer-Encoding";
    static constexpr std::string_view chunkedEncoding = "chunked";
    static constexpr std::string_view contentLength = "Content-Length";
    static constexpr std::string_view headerEnd = "\r\n\r\n";  //响应头和 body 的分隔符
    
    info.encoding = Encoding::Unknown;
    
    //第2步：遍历响应头，确定编码方式
    for (auto& keyValue : info.response.headers) {
        if (transferEncoding == keyValue.first && chunkedEncoding == keyValue.second) {
            //Transfer-Encoding: chunked → 分块传输
            info.encoding = Encoding::ChunkedEncoding;
            auto end = header.find(headerEnd);
            info.headerLength = static_cast<uint32_t>(end + headerEnd.length());
        } else if (contentLength == keyValue.first) {
            //Content-Length: N → 固定长度
            info.encoding = Encoding::ContentLength;
            std::from_chars(keyValue.second.data(), 
                           keyValue.second.data() + keyValue.second.size(), 
                           info.length);  //解析 Content-Length 的数值
            auto end = header.find(headerEnd);
            info.headerLength = static_cast<uint32_t>(end + headerEnd.length());
        }
    }
    
    //第3步：既没有 Content-Length 也没有 chunked，且不是 204 No Content → 报错
    if (info.encoding == Encoding::Unknown && 
        !HttpResponse::withoutContent(info.response.code)) {
        throw std::runtime_error("Unsupported HTTP encoding protocol");
    }
    
    //204 No Content：没有 body，响应头长度就是整个响应长度
    if (HttpResponse::withoutContent(info.response.code)) {
        info.headerLength = static_cast<uint32_t>(header.length());
    }
    
    return info;
}

//从接收缓冲区提取响应体内容（去掉响应头部分）
//目前只支持 ContentLength 编码方式
std::string HttpHelper::retrieveContent(
    const uint8_t* data,
    uint64_t length,
    std::unique_ptr<Info>& info
) {
    std::string_view sv(reinterpret_cast<const char*>(data), length);
    
    //如果还没解析过响应头，先解析
    if (!info) {
        info = std::make_unique<Info>(detect(sv));
    }
    
    if (info->encoding == Encoding::ContentLength) {
        //跳过响应头，从 headerLength 位置开始取 length 字节的 body
        return std::string(reinterpret_cast<const char*>(data) + info->headerLength, 
                          info->length);
    }
    
    //ChunkedEncoding 暂不支持提取内容（返回空）
    return {};
}

//判断 HTTP 响应是否接收完毕（核心方法）
//HTTPMessage::execute() 在 Receiving 状态每次收到数据后调用
bool HttpHelper::finished(
    const uint8_t* data,
    uint64_t length,
    std::unique_ptr<Info>& info
) {
    //首次调用：解析响应头确定编码方式
    if (!info) {
        std::string_view sv(reinterpret_cast<const char*>(data), length);
        info = std::make_unique<Info>(detect(sv));
    }
    
    //204 No Content：没有 body，收到响应头就算完成
    if (HttpResponse::withoutContent(info->response.code)) {
        return true;
    }
    
    //根据编码方式判断是否收完
    switch (info->encoding) {
        case Encoding::ContentLength:
            //收到的总字节数 >= 响应头长度 + Content-Length → 收完了
            return length >= info->headerLength + info->length;
        case Encoding::ChunkedEncoding: {
            //在数据中查找 "0\r\n\r\n" 终止块（表示分块传输结束）
            std::string_view sv(reinterpret_cast<const char*>(data), length);
            static constexpr std::string_view chunkedEnd = "0\r\n\r\n";
            auto pos = sv.find(chunkedEnd);
            bool ret = pos != sv.npos;
            if (ret) {
                //计算实际的 body 长度（终止块位置 - 响应头长度）
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

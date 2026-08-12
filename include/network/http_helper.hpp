#pragma once
#include "http_response.hpp"
#include <cstdint>
#include <memory>
#include <string_view>

namespace myblob::network {

//HTTP 响应解析工具类
//负责两件事：
//1. 判断响应是否收完（finished）—— HTTPMessage::execute() 在 Receiving 状态调用
//2. 提取响应体内容（retrieveContent）—— 用户获取实际数据
class HttpHelper {
public:
    //响应体编码方式，决定如何判断"收完了没"
    enum class Encoding : uint8_t {
        Unknown,           //还没解析出编码方式
        ContentLength,     //用 Content-Length 判断（最常见）
        ChunkedEncoding   //用 Transfer-Encoding: chunked 判断（分块传输）
    };
    
    //HTTP 响应的解析结果，这里为什么没有响应体，因为响应体不可统一解析，不同端点返回的东西完全不同
    struct Info {
        HttpResponse response;      //解析出的状态码 + 响应头
        uint64_t length{0};          //响应体长度（Content-Length 的值）
        uint32_t headerLength{0};    //响应头总字节数（含 \r\n\r\n 分隔符）
        Encoding encoding{Encoding::Unknown};  //编码方式
    };
    
    
    //从接收缓冲区提取响应体内容（去掉响应头部分）
    static std::string retrieveContent(
        const uint8_t* data,
        uint64_t length,
        std::unique_ptr<Info>& info
    );
    
    //判断 HTTP 响应是否接收完毕（核心方法）
    //HTTPMessage::execute() 在 Receiving 状态每次收到数据后调用
    //判断逻辑：
    //  ContentLength → 收到的 body 字节数 >= Content-Length
    //  ChunkedEncoding → 收到 "0\r\n" 终止块
    //  Unknown → 尝试解析响应头确定编码方式
    static bool finished(
        const uint8_t* data,
        uint64_t length,
        std::unique_ptr<Info>& info
    );

private:
    //解析 HTTP 响应头，提取状态码、Content-Length、Transfer-Encoding 等
    static Info detect(std::string_view header);
};

}  // namespace myblob::network

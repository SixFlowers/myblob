#include "network/http_response.hpp"
#include <cstring>
#include <map>
#include <stdexcept>
#include <string_view>

namespace myblob::network {

//字符串前缀匹配工具函数
static bool startsWith(std::string_view str, std::string_view prefix) {
    return str.size() >= prefix.size() && 
           strncmp(str.data(), prefix.data(), prefix.size()) == 0;
}

//将原始 HTTP 响应字符串解析为 HttpResponse 对象
//输入示例："HTTP/1.1 200 OK\r\nContent-Length: 1024\r\nETag: \"abc\"\r\n\r\n"
//输出：{type=HTTP_1_1, code=OK_200, headers={"Content-Length":"1024", "ETag":"\"abc\""}}
HttpResponse HttpResponse::deserialize(std::string_view data) {
    static constexpr std::string_view strHttp1_0 = "HTTP/1.0";
    static constexpr std::string_view strHttp1_1 = "HTTP/1.1";
    static constexpr std::string_view strNewline = "\r\n";
    static constexpr std::string_view strHeaderSeperator = ": ";
    
    HttpResponse response;
    std::string_view line;
    bool firstLine = true;
    
    //逐行解析，以 \r\n 为分隔符
    while (true) {
        auto pos = data.find(strNewline);
        if (pos == data.npos) {
            throw std::runtime_error("Invalid HttpResponse: Incomplete header!");
        }
        
        line = data.substr(0, pos);
        data = data.substr(pos + strNewline.size());  //跳过 \r\n
        
        //空行 = 响应头结束（\r\n\r\n 的第二个 \r\n）
        if (line.empty()) {
            if (!firstLine) {
                break;  //正常结束
            } else {
                throw std::runtime_error("Invalid HttpResponse: Missing first line!");
            }
        }
        
        if (firstLine) {
            //解析状态行：如 "HTTP/1.1 200 OK"
            firstLine = false;
            
            if (startsWith(line, strHttp1_0)) {
                response.type = Type::HTTP_1_0;
            } else if (startsWith(line, strHttp1_1)) {
                response.type = Type::HTTP_1_1;
            } else {
                throw std::runtime_error("Invalid HttpResponse: Needs HTTP/1.0 or HTTP/1.1!");
            }
            
            //跳过 "HTTP/1.x " 前缀，剩下的就是 "200 OK" 等
            std::string_view httpType = getResponseType(response.type);
            line = line.substr(httpType.size() + 1);
            response.code = Code::UNKNOWN;
            
            //遍历所有已知状态码，匹配前缀
            for (auto code = static_cast<uint8_t>(Code::OK_200); 
                 code <= static_cast<uint8_t>(Code::SLOW_DOWN_503); 
                 code++) {
                const std::string_view responseCode = getResponseCode(static_cast<Code>(code));
                if (startsWith(line, responseCode)) {
                    response.code = static_cast<Code>(code);
                }
            }
        } else {
            //解析响应头：如 "Content-Length: 1024"
            auto keyPos = line.find(strHeaderSeperator);
            std::string_view key, value = "";
            
            if (keyPos == line.npos) {
                throw std::runtime_error("Invalid HttpResponse: Headers need key and value!");
            } else {
                key = line.substr(0, keyPos);
                value = line.substr(keyPos + strHeaderSeperator.size());
            }
            
            response.headers.emplace(key, value);
        }
    }
    
    return response;
}

}  // namespace myblob::network

#include "network/http_response.hpp"
#include <cstring>
#include <map>
#include <stdexcept>
#include <string_view>

namespace myblob::network {

static bool startsWith(std::string_view str, std::string_view prefix) {
    return str.size() >= prefix.size() && 
           strncmp(str.data(), prefix.data(), prefix.size()) == 0;
}

HttpResponse HttpResponse::deserialize(std::string_view data) {
    static constexpr std::string_view strHttp1_0 = "HTTP/1.0";
    static constexpr std::string_view strHttp1_1 = "HTTP/1.1";
    static constexpr std::string_view strNewline = "\r\n";
    static constexpr std::string_view strHeaderSeperator = ": ";
    
    HttpResponse response;
    std::string_view line;
    bool firstLine = true;
    
    while (true) {
        auto pos = data.find(strNewline);
        if (pos == data.npos) {
            throw std::runtime_error("Invalid HttpResponse: Incomplete header!");
        }
        
        line = data.substr(0, pos);
        data = data.substr(pos + strNewline.size());
        
        if (line.empty()) {
            if (!firstLine) {
                break;
            } else {
                throw std::runtime_error("Invalid HttpResponse: Missing first line!");
            }
        }
        
        if (firstLine) {
            firstLine = false;
            
            if (startsWith(line, strHttp1_0)) {
                response.type = Type::HTTP_1_0;
            } else if (startsWith(line, strHttp1_1)) {
                response.type = Type::HTTP_1_1;
            } else {
                throw std::runtime_error("Invalid HttpResponse: Needs HTTP/1.0 or HTTP/1.1!");
            }
            
            std::string_view httpType = getResponseType(response.type);
            line = line.substr(httpType.size() + 1);
            response.code = Code::UNKNOWN;
            
            for (auto code = static_cast<uint8_t>(Code::OK_200); 
                 code <= static_cast<uint8_t>(Code::SLOW_DOWN_503); 
                 code++) {
                const std::string_view responseCode = getResponseCode(static_cast<Code>(code));
                if (startsWith(line, responseCode)) {
                    response.code = static_cast<Code>(code);
                }
            }
        } else {
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

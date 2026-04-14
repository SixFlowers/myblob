#include "network/http_request.hpp"
#include <sstream>
#include <cstring>

namespace myblob::network {

std::unique_ptr<utils::DataVector<uint8_t>> HttpRequest::serialize(const HttpRequest& request) {
    std::stringstream ss;
    ss << getRequestMethod(request.method) << " ";
    ss << request.path;
    if (!request.queries.empty()) {
        ss << "?";
        auto it = request.queries.begin();
        while (it != request.queries.end()) {
            ss << it->first << "=" << it->second;
            if (++it != request.queries.end()) {
                ss << "&";
            }
        }
    }
    ss << " " << getRequestType(request.type) << "\r\n";
    for (const auto& h : request.headers) {
        ss << h.first << ": " << h.second << "\r\n";
    }
    ss << "\r\n";
    auto str = ss.str();
    auto data = std::make_unique<utils::DataVector<uint8_t>>(str.size());
    memcpy(data->data(), str.data(), str.size());
    data->resize(str.size());
    return data;
}

HttpRequest HttpRequest::deserialize(std::string_view data) {
    HttpRequest request;
    // 简单实现：解析HTTP请求的第一行
    size_t pos = 0;
    size_t end = data.find("\r\n", pos);
    if (end == std::string_view::npos) {
        return request;
    }
    std::string_view firstLine = data.substr(pos, end - pos);
    
    // 解析方法
    size_t methodEnd = firstLine.find(' ');
    if (methodEnd != std::string_view::npos) {
        std::string_view method = firstLine.substr(0, methodEnd);
        if (method == "GET") request.method = Method::GET;
        else if (method == "PUT") request.method = Method::PUT;
        else if (method == "POST") request.method = Method::POST;
        else if (method == "DELETE") request.method = Method::DELETE;
        
        // 解析路径和查询参数
        size_t pathStart = methodEnd + 1;
        size_t pathEnd = firstLine.find(' ', pathStart);
        if (pathEnd != std::string_view::npos) {
            std::string_view pathAndQuery = firstLine.substr(pathStart, pathEnd - pathStart);
            size_t queryPos = pathAndQuery.find('?');
            if (queryPos != std::string_view::npos) {
                request.path = std::string(pathAndQuery.substr(0, queryPos));
                // 解析查询参数
                std::string_view query = pathAndQuery.substr(queryPos + 1);
                size_t qpos = 0;
                while (qpos < query.size()) {
                    size_t eqPos = query.find('=', qpos);
                    if (eqPos == std::string_view::npos) break;
                    size_t ampPos = query.find('&', eqPos);
                    if (ampPos == std::string_view::npos) ampPos = query.size();
                    std::string key(query.substr(qpos, eqPos - qpos));
                    std::string value(query.substr(eqPos + 1, ampPos - eqPos - 1));
                    request.queries[key] = value;
                    qpos = ampPos + 1;
                }
            } else {
                request.path = std::string(pathAndQuery);
            }
            
            // 解析HTTP版本
            std::string_view version = firstLine.substr(pathEnd + 1);
            if (version == "HTTP/1.0") request.type = Type::HTTP_1_0;
            else if (version == "HTTP/1.1") request.type = Type::HTTP_1_1;
        }
    }
    
    // 解析头部
    pos = end + 2;
    while (pos < data.size()) {
        end = data.find("\r\n", pos);
        if (end == std::string_view::npos || end == pos) break;
        std::string_view headerLine = data.substr(pos, end - pos);
        size_t colonPos = headerLine.find(':');
        if (colonPos != std::string_view::npos) {
            std::string key(headerLine.substr(0, colonPos));
            size_t valueStart = colonPos + 1;
            while (valueStart < headerLine.size() && headerLine[valueStart] == ' ') valueStart++;
            std::string value(headerLine.substr(valueStart));
            request.headers[key] = value;
        }
        pos = end + 2;
    }
    
    return request;
}

}

#pragma once
#include "../utils/data_vector.hpp"
#include "../utils/defer.hpp"
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
namespace myblob::network {

//HTTP 响应结构体：解析服务器返回的 HTTP 响应
//由 HttpHelper::detect() 解析响应头后填充
//被 HTTPMessage::execute() 用来判断请求是否成功
struct HttpResponse {
      //HTTP 状态码枚举（只列出 S3/Azure/GCP 常见的）
      enum class Code : uint8_t {
        OK_200,                    //GET/DELETE 成功
        CREATED_201,               //PUT 创建成功
        NO_CONTENT_204,            //DELETE 成功（无响应体）
        PARTIAL_CONTENT_206,       //Range GET 成功（部分内容）
        BAD_REQUEST_400,           //请求格式错误
        UNAUTHORIZED_401,          //签名错误/过期
        FORBIDDEN_403,             //权限不足
        NOT_FOUND_404,             //对象/桶不存在
        CONFLICT_409,              //桶已存在等冲突
        LENGTH_REQUIRED_411,       //缺少 Content-Length
        RANGE_NOT_SATISFIABLE_416, //Range 超出文件大小
        TOO_MANY_REQUESTS_429,     //请求限流
        INTERNAL_SERVER_ERROR_500, //服务器内部错误
        SERVICE_UNAVAILABLE_503,   //服务不可用
        SLOW_DOWN_503,             //S3 限速（降低发送速率）
        UNKNOWN = 255              //未知状态码
      };
      
      //HTTP 协议版本
      enum class Type : uint8_t {
          HTTP_1_0,
          HTTP_1_1
      };
      
      std::map<std::string, std::string> headers;  //响应头键值对（如 Content-Length, ETag 等）
      Code code = Code::UNKNOWN;                      //HTTP 状态码
      Type type = Type::HTTP_1_1;                     //HTTP 协议版本
      
      //判断 HTTP 状态码是否为成功（2xx）
      //HTTPMessage::execute() 用它决定进入 Finished 还是 Aborted
      static constexpr auto checkSuccess(const Code& code) {
        return (code == Code::OK_200 || code == Code::CREATED_201 || 
                code == Code::NO_CONTENT_204 || code == Code::PARTIAL_CONTENT_206);
      }
      //状态码 → 字符串（如 Code::OK_200 → "200 OK"）
      static constexpr std::string_view getResponseCode(const Code& code) noexcept {
        switch (code) {
            case Code::OK_200: return "200 OK";
            case Code::CREATED_201: return "201 Created";
            case Code::NO_CONTENT_204: return "204 No Content";
            case Code::PARTIAL_CONTENT_206: return "206 Partial Content";
            case Code::BAD_REQUEST_400: return "400 Bad Request";
            case Code::UNAUTHORIZED_401: return "401 Unauthorized";
            case Code::FORBIDDEN_403: return "403 Forbidden";
            case Code::NOT_FOUND_404: return "404 Not Found";
            case Code::CONFLICT_409: return "409 Conflict";
            case Code::LENGTH_REQUIRED_411: return "411 Length Required";
            case Code::RANGE_NOT_SATISFIABLE_416: return "416 Range Not Satisfiable";
            case Code::TOO_MANY_REQUESTS_429: return "429 Too Many Requests";
            case Code::INTERNAL_SERVER_ERROR_500: return "500 Internal Server Error";
            case Code::SERVICE_UNAVAILABLE_503: return "503 Service Unavailable";
            case Code::SLOW_DOWN_503: return "503 Slow Down";
            default: return "UNKNOWN";
        }
      }
      //状态码 → 数字（如 Code::OK_200 → 200）
      static constexpr uint64_t getResponseCodeNumber(const Code& code) noexcept {
        switch (code) {
            case Code::OK_200: return 200;
            case Code::CREATED_201: return 201;
            case Code::NO_CONTENT_204: return 204;
            case Code::PARTIAL_CONTENT_206: return 206;
            case Code::BAD_REQUEST_400: return 400;
            case Code::UNAUTHORIZED_401: return 401;
            case Code::FORBIDDEN_403: return 403;
            case Code::NOT_FOUND_404: return 404;
            case Code::CONFLICT_409: return 409;
            case Code::LENGTH_REQUIRED_411: return 411;
            case Code::RANGE_NOT_SATISFIABLE_416: return 416;
            case Code::TOO_MANY_REQUESTS_429: return 429;
            case Code::INTERNAL_SERVER_ERROR_500: return 500;
            case Code::SERVICE_UNAVAILABLE_503: return 503;
            case Code::SLOW_DOWN_503: return 503;
            default: return 0;
        }
      }
      //判断是否无响应体（204 No Content 没有 body）
      static constexpr auto withoutContent(const Code& code){
        return code ==Code::NO_CONTENT_204;
      }
      //协议版本 → 字符串（如 Type::HTTP_1_1 → "HTTP/1.1"）
      static constexpr std::string_view getResponseType(const Type&type) noexcept{
        switch(type){
            case Type::HTTP_1_0:return "HTTP/1.0";
            case Type::HTTP_1_1:return "HTTP/1.1";
            default: return "UNKNOWN";
        }
      }
      //把原始 HTTP 响应字符串解析成 HttpResponse 对象
      //如 "HTTP/1.1 200 OK\r\nContent-Length: 1024\r\n\r\n" → {code=OK_200, headers={...}}
      static HttpResponse deserialize(std::string_view data);

  };
}
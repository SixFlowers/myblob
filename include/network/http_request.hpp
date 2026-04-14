#pragma once
#include "../utils/data_vector.hpp"
#include "../utils/defer.hpp"
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace myblob::network {
  struct HttpRequest {
      enum class Method : uint8_t {
          GET,
          PUT,
          POST,
          DELETE
      };
      
      enum class Type : uint8_t {
          HTTP_1_0,
          HTTP_1_1
      };
      
      std::map<std::string, std::string> queries;
      std::map<std::string, std::string> headers;
      Method method = Method::GET;
      Type type = Type::HTTP_1_1;
      std::string path;

      /// Get the request method
      static constexpr auto getRequestMethod(const Method& method) {
          switch (method) {
              case Method::GET: return "GET";
              case Method::PUT: return "PUT";
              case Method::POST: return "POST";
              case Method::DELETE: return "DELETE";
              default: return "";
          }
      }
      /// Get the request type
      static constexpr auto getRequestType(const Type& type) {
          switch (type) {
              case Type::HTTP_1_0: return "HTTP/1.0";
              case Type::HTTP_1_1: return "HTTP/1.1";
              default: return "";
          }
      }
      /// Serialize the request
      [[nodiscard]] static std::unique_ptr<utils::DataVector<uint8_t>> serialize(const HttpRequest& request);
      /// Deserialize the request
      [[nodiscard]] static HttpRequest deserialize(std::string_view data);
  };
}

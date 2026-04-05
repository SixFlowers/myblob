
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
  };
}
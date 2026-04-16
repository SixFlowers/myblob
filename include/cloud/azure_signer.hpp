#pragma once
#include "network/http_request.hpp"
#include <string>

namespace myblob::cloud {
class AzureSigner{
public:
  [[nodiscard]] static std::string createSignedRequest(
    const std::string&serviceAccountEmail,
    const std::string&privateRSA,
    network::HttpRequest& request
  );
};
}
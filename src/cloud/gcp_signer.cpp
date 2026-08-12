#include "cloud/gcp_signer.hpp"
#include "utils/utils.hpp"
#include <algorithm>
#include <map>
#include <sstream>

namespace myblob::cloud {

using namespace std;

// ============================================================================
// GCPSigner::createSignedRequest - 创建签名请求
// ============================================================================
string GCPSigner::createSignedRequest(
    const string& serviceAccountEmail, 
    const string& privateRSA, 
    network::HttpRequest& request, 
    StringToSign& stringToSign)
{
    stringstream requestStream;
    requestStream << network::HttpRequest::getRequestMethod(request.method) << "\n";

    if (request.path.empty()) {
        requestStream << "/\n";
    } else {
        requestStream << request.path << "\n";
    }

    std::map<std::string, std::string> sorted;
    stringstream headers;
    for (const auto& h : request.headers) {
        string key = h.first;
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        sorted.emplace(key, h.second);
    }
    for (const auto& h : sorted) {
        headers << h.first << ":" << h.second << "\n";
    }

    if (!sorted.empty()) {
        stringstream signedHeaders;
        auto it = sorted.begin();
        while (it != sorted.end()) {
            signedHeaders << it->first;
            if (++it != sorted.end()) {
                signedHeaders << ";";
            }
        }
        stringToSign.signedHeaders = signedHeaders.str();
    }
    sorted.clear();

    auto it = request.queries.find("X-Goog-Date");
    if (it == request.queries.end()) {
        throw runtime_error("missing X-Goog-Date");
    }

    stringstream credentialScope;
    credentialScope << it->second.substr(0, 8) << "/" << stringToSign.region << "/" << stringToSign.service << "/goog4_request";

    request.queries.emplace("X-Goog-Algorithm", "GOOG4-RSA-SHA256");
    request.queries.emplace("X-Goog-Credential", serviceAccountEmail + "/" + credentialScope.str());
    request.queries.emplace("X-Goog-Expires", "3600");
    request.queries.emplace("X-Goog-SignedHeaders", stringToSign.signedHeaders);

    for (const auto& q : request.queries) {
        sorted.emplace(utils::encodeUrlParameters(q.first), utils::encodeUrlParameters(q.second));
    }
    request.queries.swap(sorted);

    stringstream query;
    if (!request.queries.empty()) {
        auto qit = request.queries.begin();
        while (qit != request.queries.end()) {
            query << qit->first << "=" << qit->second;
            if (++qit != request.queries.end()) {
                query << "&";
            }
        }
    }

    requestStream << query.str() << "\n";
    requestStream << headers.str() << "\n";
    requestStream << stringToSign.signedHeaders << "\n";
    requestStream << "UNSIGNED-PAYLOAD";

    auto requestString = requestStream.str();
    auto requestHash = utils::sha256Encode(
        reinterpret_cast<const uint8_t*>(requestString.data()),
        requestString.length());

    it = request.queries.find("X-Goog-Date");
    if (it == request.queries.end()) {
        throw runtime_error("missing X-Goog-Date");
    }

    stringstream stringToSignStream;
    stringToSignStream << "GOOG4-RSA-SHA256\n";
    stringToSignStream << it->second << "\n";
    stringToSignStream << credentialScope.str() << "\n";
    stringToSignStream << requestHash;

    auto stringToSignString = stringToSignStream.str();
    auto signatureData = utils::rsaSign(
        reinterpret_cast<const uint8_t*>(privateRSA.data()),
        privateRSA.size(),
        reinterpret_cast<uint8_t*>(stringToSignString.data()),
        stringToSignString.size());
    auto signature = utils::hexEncode(
        reinterpret_cast<const uint8_t*>(signatureData.first.get()),
        signatureData.second);

    return (request.path.empty() ? "/" : request.path) + "?" + query.str() + "&x-goog-signature=" + signature;
}

} // namespace myblob::cloud
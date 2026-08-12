#include "cloud/azure_signer.hpp"
#include "utils/utils.hpp"
#include <algorithm>
#include <map>
#include <sstream>

namespace myblob::cloud {

using namespace std;

string AzureSigner::createSignedRequest(
    const string& accountName,
    const string& privateRSA,
    network::HttpRequest& request)
{
    auto decodedKey = utils::base64Decode(
        reinterpret_cast<const uint8_t*>(privateRSA.data()),
        privateRSA.size());

    stringstream stringToSign;
    stringToSign << network::HttpRequest::getRequestMethod(request.method) << "\n";

    request.headers.emplace("x-ms-version", "2015-02-21");

    auto it = request.headers.find("Content-Encoding");
    if (it != request.headers.end()) {
        stringToSign << it->second << "\n";
    } else {
        stringToSign << "\n";
    }

    it = request.headers.find("Content-Language");
    if (it != request.headers.end()) {
        stringToSign << it->second << "\n";
    } else {
        stringToSign << "\n";
    }

    it = request.headers.find("Content-Length");
    if (it != request.headers.end()) {
        stringToSign << it->second;
    }
    stringToSign << "\n";

    it = request.headers.find("Content-MD5");
    stringToSign << (it != request.headers.end() ? it->second : "") << "\n";
    it = request.headers.find("Content-Type");
    stringToSign << (it != request.headers.end() ? it->second : "") << "\n";
    it = request.headers.find("Date");
    stringToSign << (it != request.headers.end() ? it->second : "") << "\n";
    it = request.headers.find("If-Modified-Since");
    stringToSign << (it != request.headers.end() ? it->second : "") << "\n";
    it = request.headers.find("If-Match");
    stringToSign << (it != request.headers.end() ? it->second : "") << "\n";
    it = request.headers.find("If-None-Match");
    stringToSign << (it != request.headers.end() ? it->second : "") << "\n";
    it = request.headers.find("If-Unmodified-Since");
    stringToSign << (it != request.headers.end() ? it->second : "") << "\n";
    it = request.headers.find("Range");
    stringToSign << (it != request.headers.end() ? it->second : "") << "\n";

    map<string, string> sorted;
    for (const auto& h : request.headers) {
        string key = h.first;
        transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
            return static_cast<char>(tolower(c));
        });
        sorted.emplace(key, h.second);
    }
    for (const auto& h : sorted) {
        if (h.first.rfind("x-ms-", 0) == 0) {
            stringToSign << h.first << ":" << h.second << "\n";
        }
    }

    stringToSign << "/" << accountName << request.path;

    stringstream query;
    if (!request.queries.empty()) {
        stringToSign << "\n";
        auto qit = request.queries.begin();
        while (qit != request.queries.end()) {
            stringToSign << qit->first << ":" << qit->second;
            query << utils::encodeUrlParameters(qit->first) << "=" << utils::encodeUrlParameters(qit->second);
            if (++qit != request.queries.end()) {
                stringToSign << "\n";
                query << "&";
            }
        }
    }

    auto sign = utils::hmacSign(
        decodedKey.first.get(),
        decodedKey.second,
        reinterpret_cast<const uint8_t*>(stringToSign.str().data()),
        stringToSign.str().size());

    request.headers.emplace("Authorization", "SharedKey " + accountName + ":" + utils::base64Encode(sign.first.get(), sign.second));

    string url = request.path.empty() ? "/" : request.path;
    if (!request.queries.empty()) {
        url += "?" + query.str();
    }
    return url;
}

}
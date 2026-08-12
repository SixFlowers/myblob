#include "cloud/provider.hpp"
#include "cloud/transaction.hpp"
#include "network/message_result.hpp"
#include <functional>
#include <iostream>

void onDownloadComplete(myblob::network::MessageResult& result) {
    if (result.success()) {
        std::cout << "Download success! Size: " << result.getSize() << std::endl;
    } else {
        std::cout << "Download failed! State: " << static_cast<int>(result.getState()) << std::endl;
    }
}

int main() {
    const char* env = getenv("MYBLOB_ENDPOINT");
    std::string endpoint = env ? env : "http://127.0.0.1:18888";
    auto provider = myblob::cloud::Provider::makeProvider(endpoint);
    if (!provider) return 1;

    myblob::cloud::Transaction transaction(provider.get());

    transaction.getObjectRequest(onDownloadComplete, "/data/file.txt");

    std::cout << "Request added, async processing..." << std::endl;

    return 0;
}
#include "network/original_message.hpp"
#include "cloud/provider.hpp"
namespace myblob::network {

OriginalMessage::OriginalMessage(
    std::unique_ptr<utils::DataVector<uint8_t>> msg,
    cloud::Provider& provider,
    uint8_t* receiveBuffer,
    uint64_t bufferSize,
    uint64_t trace
) : message(std::move(msg))
  , provider(provider)
  , result(receiveBuffer, bufferSize)
  , putData(nullptr)
  , putLength()
  , traceId(trace) {
}



}
#pragma once

namespace myblob::network {

enum class MessageState : uint8_t {
    Init,
    TLSHandshake,
    InitSending,
    Sending,
    InitReceiving,
    Receiving,
    TLSShutdown,
    Finished,
    Aborted,
    Cancelled
};

}  // namespace myblob::network

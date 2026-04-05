#pragma once
namespace myblob::network{
  enum class MessageFailureCode:uint16_t{
    None = 0,
    Socket = 1,
    Empty = 1 << 1,
    Timeout = 1 << 2,
    Send = 1 << 3,
    Recv = 1 << 4,
    HTTP = 1 << 5,
    TLS = 1 << 6
  };
}
#pragma once
namespace myblob::network{
  enum class MessageFailureCode:uint16_t{//用位运算是为了同时记录多个失败原因,主要在 HTTPMessage::execute() 状态机里，每个阶段出错时设置：
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
#pragma once

namespace myblob::network {

enum class MessageState : uint8_t {
    Init,//初始态，尚未建立连接，execute() 首次进入时从此开始
    TLSHandshake,//TLS 握手进行中，只有 HTTPS 才经过
    InitSending,//连接刚建好，准备第一次发送。和 Sending 分开是为了区分"首次进入发送阶段"和"IO 完成后继续发送"
    Sending,//正在发送请求（请求头 + putData），IO 已挂到内核，等待 complete() 事件回来
    InitReceiving,//准备接收响应，和 Receiving 分开是为了区分"首次进入接收阶段"和"IO 完成后继续接收"
    Receiving,//正在接收响应，IO 已挂到内核，等待 complete() 事件回来
    TLSShutdown,//TLS 关闭连接进行中（SSL_shutdown），只有 HTTPS 且 reuse=false 时才经过
    Finished,//响应完整接收且 HTTP 状态码 2xx，请求成功结束
    Aborted,//请求失败（HTTP 非 2xx / 发送错误 / 接收错误），不再重试
    Cancelled//多部分上传中被取消（如合并分片失败），由 Transaction 在回调里手动设置，状态机本身不会自动进入
};

}  // namespace myblob::network

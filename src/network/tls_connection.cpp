#include "network/tls_connection.hpp"
#include "network/https_message.hpp"
#include "network/tls_context.hpp"
#include "network/connection_manager.hpp"
#include "network/message_failure_code.hpp"
#include <cerrno>
#include <cassert>
#include <iostream>
#include <memory>
#include <utility>
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

namespace myblob::network {

TLSConnection::TLSConnection(TLSContext& context)
    : _message(nullptr),
      _context(context),
      _ssl(nullptr),
      _internalBio(nullptr),
      _networkBio(nullptr),
      _buffer(nullptr),
      _state(),
      _connected(false) {
}

TLSConnection::~TLSConnection() {
    destroy();
}

bool TLSConnection::init(HTTPSMessage* message) {
    if (!_message) {
        _message = message;
        if (!_context._ctx) {
            return false;
        }
        _ssl = SSL_new(_context._ctx);
        if (!_ssl) {
            return false;
        }
        SSL_set_connect_state(_ssl);
        if (!BIO_new_bio_pair(&_internalBio, static_cast<int>(_message->chunkSize),
                               &_networkBio, static_cast<int>(_message->chunkSize))) {
            SSL_free(_ssl);
            _ssl = nullptr;
            return false;
        }
        SSL_set_bio(_ssl, _internalBio, _internalBio);
        _context.reuseSession(_message->fd, _ssl);
    } else {
        _message = message;
        // SSL_free handles BIO cleanup, so just free old SSL and create new
        if (_ssl) {
            SSL_free(_ssl);   // also frees attached BIOs
            _ssl = nullptr;
        }
        _internalBio = nullptr;  // already freed by SSL_free above
        if (_networkBio) {
            BIO_free(_networkBio);   // peer BIO NOT freed by SSL_free in OpenSSL 3.x
            _networkBio = nullptr;
        }
        if (!BIO_new_bio_pair(&_internalBio, static_cast<int>(_message->chunkSize),
                               &_networkBio, static_cast<int>(_message->chunkSize))) {
            return false;
        }
        _ssl = SSL_new(_context._ctx);
        if (!_ssl) return false;
        SSL_set_connect_state(_ssl);
        SSL_set_bio(_ssl, _internalBio, _internalBio);
    }
    _message = message;
    _buffer = std::make_unique<char[]>(_message->chunkSize);
    _state.progress = Progress::Init;
    _state.reset();
    _connected = false;
    return true;
}

void TLSConnection::destroy() {
    _message = nullptr;
    _state.reset();
    _state.progress = Progress::Init;
    _connected = false;
    // OpenSSL 3.x: SSL_free 内部会调用 BIO_free_all 释放 rbio/wbio。
    // BIO pair 之间共享引用计数，所以让 SSL_free 处理 rbio/wbio，
    // 之后只需要手动释放未被 SSL 持有的 network BIO。
    if (_ssl) {
        BIO* rbio = SSL_get_rbio(_ssl);
        SSL_free(_ssl);
        _ssl = nullptr;
        // rbio 已被 SSL_free 释放，标记为 null 避免 double-free
        if (_internalBio == rbio) _internalBio = nullptr;
    }
    if (_networkBio) {
        BIO_free(_networkBio);
        _networkBio = nullptr;
    }
    if (_internalBio) {
        BIO_free(_internalBio);
        _internalBio = nullptr;
    }
}

template <typename F>
TLSConnection::Progress TLSConnection::operationHelper(
    ConnectionManager& connectionManager,
    F&& func,
    int64_t& result
) {
    if (_state.progress == Progress::Finished || _state.progress == Progress::Init) {
        auto status = func();
        auto error = SSL_get_error(_ssl, status);
        switch (error) {
            case SSL_ERROR_NONE: {
                result = status;
                return Progress::Finished;
            }
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_READ: {
                _state.progress = Progress::SendingInit;
                if (process(connectionManager) != Progress::Aborted)
                    return Progress::InProgress;
                return Progress::Aborted;
            }
            default: {
                result = status;
                return Progress::Aborted;
            }
        }
    } else if (_state.progress == Progress::Aborted) {
        return Progress::Aborted;
    } else {
        process(connectionManager);
        if (_state.progress != Progress::Finished && _state.progress != Progress::Aborted)
            return Progress::InProgress;
        else
            return operationHelper(connectionManager, func, result);
    }
}

TLSConnection::Progress TLSConnection::recv(
    ConnectionManager& connectionManager,
    char* buffer,
    int64_t bufferLength,
    int64_t& resultLength
) {
    assert(bufferLength > 0 && bufferLength <= INT_MAX);
    auto ssl = this->_ssl;
    auto sslRead = [ssl, buffer, bufferLength = static_cast<int>(bufferLength)]() {
        return SSL_read(ssl, buffer, bufferLength);
    };
    return operationHelper(connectionManager, sslRead, resultLength);
}

TLSConnection::Progress TLSConnection::send(
    ConnectionManager& connectionManager,
    const char* buffer,
    int64_t bufferLength,
    int64_t& resultLength
) {
    assert(bufferLength > 0 && bufferLength <= INT_MAX);
    auto ssl = this->_ssl;
    auto sslWrite = [ssl, buffer, bufferLength = static_cast<int>(bufferLength)]() {
        return SSL_write(ssl, buffer, bufferLength);
    };
    return operationHelper(connectionManager, sslWrite, resultLength);
}

TLSConnection::Progress TLSConnection::connect(
    ConnectionManager& connectionManager
) {
    if (!_connected) {
        int64_t unused;
        auto ssl = this->_ssl;
        auto sslConnect = [ssl]() {
            return SSL_connect(ssl);
        };
        auto status = operationHelper(connectionManager, sslConnect, unused);
        if (status == Progress::Finished) {
            _connected = true;
        }
        return status;
    }
    return Progress::Finished;
}

TLSConnection::Progress TLSConnection::shutdown(
    ConnectionManager& connectionManager,
    bool failedOnce
) {
    int64_t unused = 0;
    auto ssl = this->_ssl;
    auto sslShutdown = [ssl]() {
        return SSL_shutdown(ssl);
    };
    auto status = operationHelper(connectionManager, sslShutdown, unused);
    if (status == Progress::Finished) {
        _context.cacheSession(_message->fd, ssl);
    } else if (status == Progress::Aborted) {
        if (!failedOnce) {
            return shutdown(connectionManager, true);
        }
        _context.dropSession(_message->fd);
    }
    return status;
}

TLSConnection::Progress TLSConnection::process(
    ConnectionManager& connectionManager
) {
    switch (_state.progress) {
        case Progress::SendingInit: {
            _state.reset();
            _state.internalBioWrite = BIO_ctrl_pending(_networkBio);
            if (_state.internalBioWrite) {
                auto readSize = _message->chunkSize > _state.internalBioWrite 
                    ? _state.internalBioWrite 
                    : _message->chunkSize;
                assert(readSize <= INT_MAX);
                _state.networkBioRead = BIO_read(
                    _networkBio, 
                    _buffer.get(), 
                    static_cast<int>(readSize)
                );
            }
        } // fallthrough
        
        case Progress::Sending: {
            if (_state.internalBioWrite) {
                if (_state.progress == Progress::Sending) {
                    if (_message->request && _message->request->length > 0) {
                        _state.socketWrite += static_cast<uint64_t>(_message->request->length);
                    } else if (_message->request && 
                              _message->request->length != -EINPROGRESS && 
                              _message->request->length != -EAGAIN) {
                        if (_message->request->length == -ECANCELED || 
                            _message->request->length == -EINTR) {
                            _state.progress = Progress::Aborted;
                            return _state.progress;
                        } else {
                            _state.progress = Progress::ReceivingInit;
                        }
                    }
                }
                if (_state.networkBioRead >= 0 && 
                    static_cast<size_t>(_state.networkBioRead) != _state.socketWrite) {
                    _state.progress = Progress::Sending;
                    auto writeSize = static_cast<size_t>(_state.networkBioRead) - _state.socketWrite;
                    const uint8_t* ptr = reinterpret_cast<uint8_t*>(_buffer.get()) + _state.socketWrite;
                    _message->request = std::make_unique<Socket::Request>(Socket::Request{
                        .data = {.sendData = ptr}, 
                        .length = static_cast<int64_t>(writeSize), 
                        .fd = _message->fd, 
                        .event = Socket::EventType::write, 
                        .userData = _message
                    });
                    if (writeSize <= _message->chunkSize) {
                        connectionManager.getSocket().send_to(
                            *_message->request, 
                            _message->tcpSettings.timeout
                        );
                    } else {
                        connectionManager.getSocket().send(*_message->request);
                    }
                    return _state.progress;
                } else {
                    _state.progress = Progress::ReceivingInit;
                }
            } else {
                _state.progress = Progress::ReceivingInit;
            }
        } // fallthrough
        
        case Progress::ReceivingInit: {
            _state.internalBioRead = BIO_ctrl_get_read_request(_networkBio);
        } // fallthrough
        
        case Progress::Receiving: {
            if (_state.internalBioRead) {
                if (_state.progress == Progress::Receiving) {
                    if (_message->request && _message->request->length == 0) {
                        auto code = _message->originalMessage->result.getFailureCode();
                        _message->originalMessage->result.setFailureCode(code | static_cast<uint16_t>(MessageFailureCode::Empty));
                        _state.progress = Progress::Aborted;
                        return _state.progress;
                    } else if (_message->request && _message->request->length > 0) {
                        assert(_message->request->length <= INT_MAX);
                        _state.networkBioWrite += BIO_write(
                            _networkBio, 
                            _buffer.get() + _state.socketRead, 
                            static_cast<int>(_message->request->length)
                        );
                        _state.socketRead += static_cast<size_t>(_message->request->length);
                    } else if (_message->request && 
                              _message->request->length != -EINPROGRESS && 
                              _message->request->length != -EAGAIN) {
                        if (_message->request->length == -ECANCELED || 
                            _message->request->length == -EINTR) {
                            auto code = _message->originalMessage->result.getFailureCode();
                            _message->originalMessage->result.setFailureCode(code | static_cast<uint16_t>(MessageFailureCode::Timeout));
                        } else {
                            auto code = _message->originalMessage->result.getFailureCode();
                            _message->originalMessage->result.setFailureCode(code | static_cast<uint16_t>(MessageFailureCode::Recv));
                        }
                        _state.progress = Progress::Aborted;
                        return _state.progress;
                    }
                }
                if (!_state.networkBioWrite || 
                    (_state.networkBioWrite >= 0 && 
                     static_cast<size_t>(_state.networkBioWrite) != _state.socketRead)) {
                    _state.progress = Progress::Receiving;
                    uint64_t readSize = _message->chunkSize > (_state.internalBioRead - _state.socketRead) 
                        ? (_state.internalBioRead - _state.socketRead) 
                        : _message->chunkSize;
                    uint8_t* ptr = reinterpret_cast<uint8_t*>(_buffer.get()) + _state.socketRead;
                    assert(readSize <= INT64_MAX);
                    _message->request = std::make_unique<Socket::Request>(Socket::Request{
                        .data = {.recvData = ptr}, 
                        .length = static_cast<int64_t>(readSize), 
                        .fd = _message->fd, 
                        .event = Socket::EventType::read, 
                        .userData = _message
                    });
                    connectionManager.getSocket().recv_to(
                        *_message->request,
                        _message->tcpSettings.timeout,
                        0
                    );
                    return _state.progress;
                } else {
                    _state.progress = Progress::Finished;
                    return _state.progress;
                }
            } else {
                _state.progress = Progress::Finished;
                return _state.progress;
            }
        }
        
        default: {
            return _state.progress;
        }
    }
}

}
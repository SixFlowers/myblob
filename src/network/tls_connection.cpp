#include "network/tls_connection.hpp"
#include "network/https_message.hpp"
#include "network/tls_context.hpp"
#include "network/connection_manager.hpp"
#include <cassert>
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
        BIO_new_bio_pair(&_internalBio, _message->chunkSize, &_networkBio, _message->chunkSize);
        SSL_set_bio(_ssl, _internalBio, _internalBio);
        _context.reuseSession(_message->fd, _ssl);
    } else {
        _message = message;
        BIO_free(_networkBio);
        BIO_new_bio_pair(&_internalBio, _message->chunkSize, &_networkBio, _message->chunkSize);
        SSL_set_bio(_ssl, _internalBio, _internalBio);
        _connected = true;
    }
    _message = message;
    _buffer = std::make_unique<char[]>(_message->chunkSize);
    return true;
}

void TLSConnection::destroy() {
    _state.reset();
    if (_ssl) {
        SSL_free(_ssl);
        BIO_free(_networkBio);
        _ssl = nullptr;
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
                    return Progress::Progress;
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
            return Progress::Progress;
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
        return operationHelper(connectionManager, sslConnect, unused);
    }
    return _state.progress;
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
                    if (_message->request) {
                        *_message->request = Socket::Request{
                            .data = {.sendData = ptr}, 
                            .length = static_cast<int64_t>(writeSize), 
                            .fd = _message->fd, 
                            .event = Socket::EventType::write, 
                            .userData = _message
                        };
                        if (writeSize <= _message->chunkSize) {
                            connectionManager.getPollSocket().send_to(
                                *_message->request, 
                                _message->tcpSettings.timeout
                            );
                        } else {
                            connectionManager.getPollSocket().send(*_message->request);
                        }
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
                    if (_message->request) {
                        *_message->request = Socket::Request{
                            .data = {.data = ptr}, 
                            .length = static_cast<int64_t>(readSize), 
                            .fd = _message->fd, 
                            .event = Socket::EventType::read, 
                            .userData = _message
                        };
                        connectionManager.getPollSocket().recv_to(
                            *_message->request,
                            _message->tcpSettings.timeout,
                            _message->tcpSettings.recvNoWait ? MSG_DONTWAIT : 0
                        );
                    }
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
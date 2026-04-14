#include "cloud/transaction.hpp"
#include <iostream>
#include <thread>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>

namespace myblob::cloud {

Transaction::Transaction()
    : provider_(nullptr) {
}

Transaction::Transaction(Provider* provider)
    : provider_(provider) {
}

void Transaction::setProvider(Provider* provider) {
    provider_ = provider;
}

bool Transaction::getObjectRequest(
    const std::string& remotePath,
    std::pair<uint64_t, uint64_t> range,
    uint8_t* result,
    uint64_t capacity
) {
    if (!provider_) {
        return false;
    }
    
    auto msg = std::make_unique<network::OriginalMessage>(
        provider_->getRequest(remotePath, range),
        *provider_,
        result,
        capacity
    );
    
    messages_.push_back(std::move(msg));
    return true;
}

void Transaction::execute(){
    requestHolder_.clear();
    
    if(!connMgr_){
        connMgr_ = std::make_unique<network::ConnectionManager>();
    }
    auto& pollSocket = connMgr_->getPollSocket();
    for(auto&msg:messages_){
        if(msg->result.getState() == network::MessageState::Init){
            auto conn = connMgr_->getConnection(
                msg->provider.getAddress(),
                msg->provider.getPort(),
                msg->provider.isHTTPS()
            );
            if(conn && conn->isConnected()){
                int fd = conn->getSocket();
                msg->result.setState(network::MessageState::Sending);
                auto req = std::make_unique<network::Socket::Request>();
                req->fd = fd;
                req->event = network::Socket::EventType::write;
                req->userData = msg.get();
                requestHolder_.push_back(std::move(req));
                pollSocket.send(*requestHolder_.back());
            }else{
                msg->result.setState(network::MessageState::Aborted);
            }
        }
    }
    bool allDone = false;
    while(!allDone){
        int32_t completed = pollSocket.submit();
        while(auto*req = pollSocket.complete()){
            auto*msg = static_cast<network::OriginalMessage*>(req->userData);
            if(msg->result.getState() == network::MessageState::Sending){
                ssize_t sent = ::send(req->fd,msg->message->data(),msg->message->size(),0);
                if(sent > 0){
                    msg->result.setState(network::MessageState::Receiving);
                    auto recvReq = std::make_unique<network::Socket::Request>();
                    recvReq->fd = req->fd;
                    recvReq->event = network::Socket::EventType::read;
                    recvReq->userData = msg;
                    recvReq->length = 0;
                    recvReq->data.recvData = nullptr;
                    requestHolder_.push_back(std::move(recvReq));
                    pollSocket.recv(*requestHolder_.back());
                }
            }else if(msg->result.getState() == network::MessageState::Receiving){
                uint8_t buffer[8192];
                ssize_t received = ::recv(req->fd,buffer,sizeof(buffer),0);
                if(received > 0){
                    auto* dataVector = new utils::DataVector<uint8_t>();
                    dataVector->resize(received);
                    memcpy(dataVector->data(),buffer,received);
                    msg->setResultVector(dataVector);
                    msg->result.setState(network::MessageState::Finished);
                    if(msg->requiresFinish()){
                        msg->finish();
                    }
                }else if(received == 0){
                    msg->result.setState(network::MessageState::Finished);
                    if(msg->requiresFinish()){
                        msg->finish();
                    }
                }
            }
        }
        allDone = true;
        for(auto&msg :messages_){
            auto state = msg->result.getState();
            if (state != network::MessageState::Finished &&
                state != network::MessageState::Aborted &&
                state != network::MessageState::Cancelled) {
                allDone = false;
                break;
            }
        }
        if(completed == 0 && !allDone){
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void Transaction::executeAsync() {
    std::thread([this]() {
        execute();
    }).detach();
}

}  // namespace myblob::cloud

#include "network/message_result.hpp"
#include "network/http_helper.hpp"
#include <cstring>

namespace myblob::network {
  MessageResult::MessageResult()
    : state_(MessageState::Init) {
  }

  MessageResult::MessageResult(uint8_t* data, uint64_t size)
    : dataVector_(std::make_unique<utils::DataVector<uint8_t>>(data, size)),
      state_(MessageState::Init) {
  }

  MessageResult::MessageResult(utils::DataVector<uint8_t>* dataVector)
    : dataVector_(dataVector),
      state_(MessageState::Init) {
  }
  std::string_view MessageResult::getResult() const{
    if(!dataVector_ || dataVector_->size() == 0){
      return {};
    }
    return std::string_view(reinterpret_cast<const char*>(dataVector_->cdata()),dataVector_->size());
  }
  const uint8_t* MessageResult::getData() const{
    return dataVector_ ? dataVector_->cdata() : nullptr;
  }
  uint8_t* MessageResult::getData() {
    return dataVector_ ? dataVector_->data() : nullptr;
  }
  uint64_t MessageResult::getSize() const {
    return response_ ? response_->length : 0;
  }
  uint64_t MessageResult::getOffset() const {
    return response_ ? response_->headerLength : 0;
  }
  MessageState MessageResult::getState() const{
    return state_.load();
  }
  uint16_t MessageResult::getFailureCode() const {
    return failureCode_;
  }
  bool MessageResult::success() const{
    return state_.load() == MessageState::Finished && failureCode_ == 0;
  }
  utils::DataVector<uint8_t>& MessageResult::getDataVector(){
    static utils::DataVector<uint8_t> empty;
    return dataVector_ ? *dataVector_ : empty;
  }
  std::unique_ptr<utils::DataVector<uint8_t>> MessageResult::moveDataVector() {
    return std::move(dataVector_);
  }
  std::unique_ptr<uint8_t[]> MessageResult::moveData() {
    return dataVector_ ? dataVector_->transferBuffer() : nullptr;
  }
  std::string_view MessageResult::getErrorResponse() const {
    if (response_)
        return std::string_view(
            reinterpret_cast<const char*>(dataVector_->data()),
            dataVector_->size()
        );
    return {};
  }
  std::string_view MessageResult::getResponseCode() const {
    if (response_)
        return HttpResponse::getResponseCode(response_->response.code);
    return {};
  } 

  uint64_t MessageResult::getResponseCodeNumber() const {
    if (response_)
        return HttpResponse::getResponseCodeNumber(response_->response.code);
    return 0;
  }

  bool MessageResult::owned() const {
    return dataVector_ && dataVector_->owned();
  }





}
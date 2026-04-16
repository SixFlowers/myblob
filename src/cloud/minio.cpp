#include "cloud/minio.hpp"

namespace myblob::cloud {
std::string MinIO::getAddress() const{
  if(!_settings.endpoint.empty()){
    return _settings.endpoint;
  }
}
Provider::Instance MinIO::getInstanceDetails(myblob::network::TaskedSendReceiverHandle&sendReceiver){
  return {"minio-server","","minio","",getAddress(),getPort()};
}
}
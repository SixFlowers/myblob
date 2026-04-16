#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace myblob::utils {
//URL编码
  std::string encodeUrlParameters(const std::string&encode);
  //Base64编码
  std::string base64Encode(const uint8_t*input,uint64_t length);
  //Base64解码
  std::pair<std::unique_ptr<uint8_t[]>,uint64_t> base64Decode(const uint8_t*input,uint64_t length);
  //哈希函数
  std::string sha256Encode(const uint8_t*data,uint64_t length);
  //MD5哈希编码
  std::string md5Encode(const uint8_t*data,uint64_t length);
  //HMAC签名(AWS 签名核心)
  std::pair<std::unique_ptr<uint8_t[]>,uint64_t> hmacSign(const uint8_t*keyData,uint64_t keyLength,
  const uint8_t* msgData,uint64_t msgLength);
  //十六进制编码
  std::string hexEncode(const uint8_t*input,uint64_t length,bool upper = false);



}
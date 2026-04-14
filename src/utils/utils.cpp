#include "utils/utils.hpp"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#include <iomanip>
#include <sstream>
#include <memory>
#include <stdexcept>

namespace myblob::utils {

using namespace std;

string encodeUrlParameters(const string& encode) {
  string result;
  for(auto c:encode){
    if(isalnum(c) || c== '-' || c == '_' || c == '.' || c == '~'){
      result += c;
    }else{
      result += "%";
      const char hex[] = "0123456789ABCDEF";
      result += hex[static_cast<unsigned char>(c)>>4];
      result += hex[static_cast<unsigned char>(c)&0x0F];
    }
  }
  return result;
}

string base64Encode(const uint8_t*input,uint64_t length){
  if(length > INT_MAX){
    throw runtime_error("Input too long");
  }
  auto baseLength = 4*((length+2)/3);
  auto buffer = make_unique<char[]>(baseLength+1);
  auto encodeLength = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(buffer.get()),input,static_cast<int>(length));
  if(encodeLength<0||static_cast<unsigned>(encodeLength)!=baseLength){
    throw runtime_error("Base64 encode error!");
  }
  return string(buffer.get(),static_cast<unsigned>(encodeLength));
}

pair<unique_ptr<uint8_t[]>, uint64_t> base64Decode(const uint8_t* input, uint64_t length) {
  if (length > INT_MAX) throw runtime_error("Input too long");
  if(length == 0){
    auto buffer = make_unique<uint8_t[]>(1);
    return {move(buffer),0};
  }
  auto baseLength = 3*length / 4;
  auto buffer = make_unique<uint8_t[]>(baseLength+1);
  auto decodeLength=EVP_DecodeBlock(reinterpret_cast<unsigned char*>(buffer.get()),input,static_cast<int>(length));
  if(decodeLength<0){
    throw runtime_error("Base64 decode error!");
  }
  while(length > 0 && input[length-1]=='='){
    --decodeLength;
    --length;
  }
  return {move(buffer),static_cast<uint64_t>(decodeLength)};
}

string hexEncode(const uint8_t* input, uint64_t length, bool upper) {
  const char hex[] = "0123456789abcdef";
  string output;
  output.reserve(length << 1);
  for(auto i = 0u;i < length;i++){
    output.push_back(upper ? static_cast<char>(toupper(hex[input[i]>>4])):hex[input[i]>>4]);
    output.push_back(upper ? static_cast<char>(toupper(hex[input[i]&0x0F])):hex[input[i]&0x0F]);
  }
  return output;
}

//SHA256哈希
string sha256Encode(const uint8_t* data, uint64_t length) {
  unsigned char hash[SHA256_DIGEST_LENGTH];
  unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> mdctx(EVP_MD_CTX_new(),EVP_MD_CTX_free);
  if(!mdctx){
    throw runtime_error("OpenSSL Error: Failed to create MD context");
  }
  //初始化算法
  if(EVP_DigestInit_ex(mdctx.get(),EVP_sha256(),nullptr)<=0){
    throw runtime_error("OpenSSL Error: DigestInit failed");
  }
  //传入数据
  if(EVP_DigestUpdate(mdctx.get(),data,length)<= 0){
    throw runtime_error("OpenSSL Error: DigestUpdate failed");
  }
  unsigned int digestLength = SHA256_DIGEST_LENGTH;
  //获取哈希结果
  if(EVP_DigestFinal_ex(mdctx.get(),hash,&digestLength)<=0){
    throw runtime_error("OpenSSL Error: DigestFinal failed");
  }
  return hexEncode(hash,SHA256_DIGEST_LENGTH);
}

//MD5哈希
string md5Encode(const uint8_t* data, uint64_t length) {
  unsigned char hash[MD5_DIGEST_LENGTH];
  unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> mdctx(EVP_MD_CTX_new(),EVP_MD_CTX_free);
  if(!mdctx){
    throw runtime_error("OpenSSL Error: Failed to create MD context");
  }
  if(EVP_DigestInit_ex(mdctx.get(),EVP_md5(),nullptr) <= 0){
    throw runtime_error("OpenSSL Error: DigestInit failed");
  }
  if(EVP_DigestUpdate(mdctx.get(),data,length)<=0){
    throw runtime_error("OpenSSL Error: DigestUpdate failed");
  }
  unsigned int digestLength = MD5_DIGEST_LENGTH;
  if(EVP_DigestFinal_ex(mdctx.get(),hash,&digestLength)<=0){
    throw runtime_error("OpenSSL Error: DigestFinal failed");
  }
  return string(reinterpret_cast<char*>(hash),MD5_DIGEST_LENGTH);
}

pair<unique_ptr<uint8_t[]>, uint64_t> hmacSign(
    const uint8_t* keyData, uint64_t keyLength,
    const uint8_t* msgData, uint64_t msgLength)
{
  unique_ptr<EVP_MAC,decltype(&EVP_MAC_free)> mac(EVP_MAC_fetch(nullptr,"HMAC",nullptr),EVP_MAC_free);
  if(!mac){
    throw runtime_error("OpenSSL Error: Failed to fetch HMAC");
  }
  OSSL_PARAM params[2];
  string digest = "SHA2-256";
  params[0] = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST,digest.data(),digest.size());
  params[1] = OSSL_PARAM_construct_end();
  unique_ptr<EVP_MAC_CTX,decltype(&EVP_MAC_CTX_free)> mctx(EVP_MAC_CTX_new(mac.get()),EVP_MAC_CTX_free);
  if(!mctx){
    throw runtime_error("OpenSSL Error: Failed to create MAC context");
  }
  if(EVP_MAC_init(mctx.get(),keyData,keyLength,params)<=0){
    throw runtime_error("OpenSSL Error: MAC init failed");
  }
  if(EVP_MAC_update(mctx.get(),msgData,msgLength)<=0){
    throw runtime_error("OpenSSL Error: MAC update failed");
  }
  size_t len;
  if(EVP_MAC_final(mctx.get(),nullptr,&len,0)<=0){
    throw runtime_error("OpenSSL Error: MAC final (get length) failed");
  }
  auto hash = make_unique<uint8_t[]>(len);
  if(EVP_MAC_final(mctx.get(),hash.get(),&len,len)<=0){
    throw runtime_error("OpenSSL Error: MAC final failed");
  }
  return {move(hash),static_cast<uint64_t>(len)};
}

}

#pragma once
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <netdb.h>
#include <netinet/tcp.h>

namespace myblob::network {
class TLSConnection;
// DnsEntry - DNS 解析结果条目
struct DnsEntry{
  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addr;  // DNS 地址信息
  int cachePriority = 0;
  DnsEntry() : addr(nullptr,freeaddrinfo){}
  DnsEntry(std::unique_ptr<addrinfo,decltype(&freeaddrinfo)>address,int priority = 0):
  addr(std::move(address)),cachePriority(priority){}
};
//SocketEntry - Socket 连接条目（缓存的基本单元）
struct SocketEntry{
  std::unique_ptr<DnsEntry> dns;
  // std::unique_ptr<TLSConnection> tls;  // 暂时注释掉，需要完整类型
  size_t timestamp;
  int32_t fd;
  unsigned port;
  std::string hostname;
  SocketEntry(std::string host,unsigned p):
  timestamp(0),fd(-1),port(p),hostname(std::move(host)){}
};
class Cache {
protected:
  //主机名->SocketEntry的多值映射(一个主机连接多个)
  std::multimap<std::string,std::unique_ptr<SocketEntry>>_cache;
  //FIFO淘汰:时间戳->SocketEntry指针
  std::map<size_t,SocketEntry*>_fifo;
  size_t _timestamp = 0;
  //默认优先级(越小越优先)
  int _defaultPriority = 8;
public:
  virtual ~Cache();
  /// 解析主机名和端口，返回可用的 SocketEntry
    /// 如果有缓存的连接，直接返回；否则创建新的
  virtual std::unique_ptr<SocketEntry>resolve(const std::string& hostname,unsigned port,bool tls);
  //开始socket及时(用于吞吐量计算)
  virtual void startSocket(int){}
  //停止socket 并决定是否缓存
  virtual void stopSocket(std::unique_ptr<SocketEntry> socketEntry,uint64_t bytes,unsigned cacheEntryies,bool reuseSocket);
  //关闭socket(不缓存)
  virtual void shutdownSocket(std::unique_ptr<SocketEntry> socketEntry,
        unsigned cachedEntries);
  void setDefaultPriority(int priority){
    _defaultPriority = priority;
  }
  size_t size() const{
    return _cache.size();
  }
  void clear(){
    _cache.clear();
    _fifo.clear();
  }
  //获取顶级域名
  //例如："www.example.com" -> "com"
  static std::string_view tld(std::string_view domain);
};

}
#pragma once
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace myblob::utils {

struct TimingHelper {
  uint64_t size;
  //开始时间
  std::chrono::steady_clock::time_point start;
  //首次接收时间
  std::chrono::steady_clock::time_point receive;
  //完成时间
  std::chrono::steady_clock::time_point finish;
};

class Timer {
public:
  enum Steps : uint64_t {
    Overall,            //总体耗时
    Upload,             //上传耗时 
    Download,           //下载耗时
    Submit,             //提交耗时
    Request,            //请求耗时
    Response,           //响应耗时
    Buffer,             //缓冲区操作耗时
    BufferCapacity,     //缓冲区容量调整耗时
    BufferResize,       //缓冲区分配耗时
    Finishing,          //完成处理耗时
    Waiting,            //等待耗时
    Queue,              //队列操作耗时
    SocketCreation,     //Socket创建耗时
    BufferCapacitySend  //发送缓冲区容量耗时
  };

  class TimerGuard {
    Steps step;
    Timer* timer;
  public:
    TimerGuard(Steps s, Timer* t) : step(s), timer(t) {
      if(timer) {
        timer->start(step);
      }
    }
    ~TimerGuard() {
      if(timer) {
        timer->stop(step);
      }
    }
  };

private:
  //当前计时开始时间
  std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> _currentTimer;
  //累计耗时
  std::unordered_map<uint64_t, uint64_t> _totalTimer;
  //输出流
  std::ostream* _outStream;
  //是否写入表头
  bool _writeHeader;
  //表头信息
  std::string _headerInfo;
  //内容信息
  std::string _contentInfo;

public:
  Timer() : _outStream(nullptr), _writeHeader(true) {}
  Timer(std::ostream* outStream, bool writeHeader = true) : _outStream(outStream), _writeHeader(writeHeader) {}
  Timer(const Timer&) = default;
  ~Timer() {
    if(_outStream) {
      printResult(*_outStream);
    }
  }

  //设置输出流
  void setOutStream(std::ostream* outStream, bool writeHeader = true) {
    _outStream = outStream;
    _writeHeader = writeHeader;
  }

  //设置信息
  void setInfo(std::string headerInfo, std::string contentInfo) {
    _headerInfo = std::move(headerInfo);
    _contentInfo = std::move(contentInfo);
  }

  //开始计时
  void start(Steps s) {
    _currentTimer[static_cast<uint64_t>(s)] = std::chrono::steady_clock::now();
  }

  //停止计时并累加
  void stop(Steps s) {
    auto end = std::chrono::steady_clock::now();
    auto it = _currentTimer.find(static_cast<uint64_t>(s));
    if(it != _currentTimer.end()) {
      auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - it->second).count();
      _totalTimer[static_cast<uint64_t>(s)] += duration;
      _currentTimer.erase(it);
    }
  }

  void printResult(std::ostream& s) {
    if(_writeHeader && !_headerInfo.empty()) {
      s << _headerInfo << std::endl;
    }
    if(!_contentInfo.empty()) {
      s << _contentInfo << std::endl;
    }
    s << "Timer Results:" << std::endl;
    const char* stepNames[] = {
      "Overall",
      "Upload",
      "Download",
      "Submit",
      "Request",
      "Response",
      "Buffer",
      "BufferCapacity",
      "BufferResize",
      "Finishing",
      "Waiting",
      "Queue",
      "SocketCreation",
      "BufferCapacitySend"
    };
    for(const auto& [step, time] : _totalTimer) {
      if(step < sizeof(stepNames) / sizeof(stepNames[0])) {
        s << "  " << stepNames[step] << ": " << time / 1000000.0 << " ms" << std::endl;
      }
    }
  }
};

}

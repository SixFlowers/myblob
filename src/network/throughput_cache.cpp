#include "network/throughput_cache.hpp"
#include <iostream>

namespace myblob::network {

void ThroughputCache::startSocket(int fd) {
    _fdMap[fd] = std::chrono::steady_clock::now();
}

double ThroughputCache::getPercentile(int pct) const {
    if (_throughput.empty()) {
        return 0.0;
    }
    auto sorted = _throughput;
    std::sort(sorted.begin(), sorted.end());
    auto idx = static_cast<size_t>(static_cast<double>(pct) / 100.0 * (sorted.size() - 1));
    if (idx >= sorted.size()) idx = sorted.size() - 1;
    return sorted[idx];
}

double ThroughputCache::getAverageThroughput() const {
    if (_throughput.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (double t : _throughput) {
        sum += t;
    }
    return sum / static_cast<double>(_throughput.size());
}

void ThroughputCache::stopSocket(
    std::unique_ptr<SocketEntry> socketEntry,
    uint64_t bytes,
    unsigned cachedEntries,
    bool reuseSocket)
{
    if (!socketEntry) {
        return;
    }
    int fd = socketEntry->fd;
    auto it = _fdMap.find(fd);
    if (it != _fdMap.end()) {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count();
        double throughput = 0.0;
        if (duration > 0) {
            throughput = (static_cast<double>(bytes) / static_cast<double>(duration)) * 1000.0;
        }

        // 写入历史环形缓冲区
        if (_throughput.size() < _maxHistory) {
            _throughput.push_back(throughput);
        } else {
            _throughput[_throughputIterator % _maxHistory] = throughput;
        }
        _throughputIterator++;

        _fdMap.erase(it);

        // 百分位分级缓存决策
        bool shouldCache = false;
        if (reuseSocket) {
            auto count = _throughput.size();
            if (count >= 6) {
                // 丰富的历史数据 → 使用百分位判断
                auto p33 = getPercentile(33);
                auto p16 = getPercentile(16);

                if (throughput >= p16) {
                    socketEntry->dns->cachePriority += 2;  // 前 16% → +2
                }
                if (throughput >= p33) {
                    socketEntry->dns->cachePriority += 1;  // 前 33% → +1
                }
                shouldCache = (throughput >= p33);
            } else {
                // 历史不足 → 退回简单平均判断
                double avg = getAverageThroughput();
                shouldCache = (throughput >= avg * 0.8);
            }
        }

        Cache::stopSocket(std::move(socketEntry), bytes, cachedEntries, shouldCache);
    } else {
        Cache::stopSocket(std::move(socketEntry), bytes, cachedEntries, reuseSocket);
    }
}

} // namespace myblob::network

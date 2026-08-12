#pragma once
#include "network/cache.hpp"
#include <vector>
#include <unordered_map>
#include <chrono>
#include <algorithm>

namespace myblob::network {

/// 基于吞吐量百分位的连接缓存策略
///
/// 优于简单平均判断：高速连接获得更高缓存优先级，
/// 低速连接在淘汰时优先丢弃。
///
/// 分位规则（历史 ≥ 6 条时）：
///   - 吞吐量 ≥ P33（33 百分位）→ priority += 1
///   - 吞吐量 ≥ P16（16 百分位）→ priority += 2
///   高吞吐量连接可获得 +3 优先级提升
///
/// 历史不足时退回简单平均判断。
class ThroughputCache : public Cache {
private:
    std::vector<double> _throughput;     // 吞吐量历史记录（环形缓冲区）
    uint64_t _throughputIterator = 0;    // 环形写入位置
    static constexpr unsigned _maxHistory = 128;
    std::unordered_map<int, std::chrono::steady_clock::time_point> _fdMap;

public:
    ThroughputCache() {
        _throughput.reserve(_maxHistory);
        _defaultPriority = 2;  // 基础优先级低于默认 Cache
    }

    void startSocket(int fd) override;

    /// 停止 socket 并基于百分位计算缓存优先级
    void stopSocket(std::unique_ptr<SocketEntry> socketEntry,
                    uint64_t bytes,
                    unsigned cachedEntries,
                    bool reuseSocket) override;

    double getAverageThroughput() const;

    /// 获取指定百分位的吞吐量值（0~100）
    double getPercentile(int pct) const;

    const std::vector<double>& getThroughputHistory() const {
        return _throughput;
    }
};

} // namespace myblob::network

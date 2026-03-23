#pragma once
#include "types.h"
#include <mutex>
#include <atomic>

namespace gateway {

class Metrics {
public:
    static Metrics& instance();

    // Counters
    void inc_requests();
    void inc_errors();
    void inc_cache_hits();
    void inc_cache_misses();

    // Gauges
    void observe_latency(double ms);
    void observe_tokens(int count);

    // Prometheus-compatible output
    std::string to_prometheus() const;

    // JSON summary
    json to_json() const;

    struct Snapshot {
        uint64_t total_requests   = 0;
        uint64_t total_errors     = 0;
        uint64_t cache_hits       = 0;
        uint64_t cache_misses     = 0;
        double   avg_latency_ms   = 0;
        double   p99_latency_ms   = 0;
        uint64_t total_tokens     = 0;
        double   uptime_seconds   = 0;
    };
    Snapshot snapshot() const;

private:
    Metrics();
    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> total_errors_{0};
    std::atomic<uint64_t> cache_hits_{0};
    std::atomic<uint64_t> cache_misses_{0};
    std::atomic<uint64_t> total_tokens_{0};

    mutable std::mutex latency_mu_;
    std::vector<double> latencies_;
    TimePoint start_time_;
};

} // namespace gateway

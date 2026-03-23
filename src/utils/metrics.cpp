#include "utils/metrics.h"
#include <algorithm>
#include <numeric>
#include <sstream>

namespace gateway {

Metrics& Metrics::instance() {
    static Metrics inst;
    return inst;
}

Metrics::Metrics() : start_time_(Clock::now()) {}

void Metrics::inc_requests()    { total_requests_++; }
void Metrics::inc_errors()      { total_errors_++; }
void Metrics::inc_cache_hits()  { cache_hits_++; }
void Metrics::inc_cache_misses(){ cache_misses_++; }

void Metrics::observe_latency(double ms) {
    std::lock_guard<std::mutex> lock(latency_mu_);
    latencies_.push_back(ms);
    // Keep last 10000 observations
    if (latencies_.size() > 10000) {
        latencies_.erase(latencies_.begin(), latencies_.begin() + 5000);
    }
}

void Metrics::observe_tokens(int count) {
    total_tokens_ += count;
}

Metrics::Snapshot Metrics::snapshot() const {
    Snapshot s;
    s.total_requests = total_requests_.load();
    s.total_errors   = total_errors_.load();
    s.cache_hits     = cache_hits_.load();
    s.cache_misses   = cache_misses_.load();
    s.total_tokens   = total_tokens_.load();

    auto elapsed = std::chrono::duration<double>(Clock::now() - start_time_);
    s.uptime_seconds = elapsed.count();

    {
        std::lock_guard<std::mutex> lock(latency_mu_);
        if (!latencies_.empty()) {
            double sum = std::accumulate(latencies_.begin(), latencies_.end(), 0.0);
            s.avg_latency_ms = sum / latencies_.size();

            auto sorted = latencies_;
            std::sort(sorted.begin(), sorted.end());
            size_t p99_idx = static_cast<size_t>(sorted.size() * 0.99);
            s.p99_latency_ms = sorted[std::min(p99_idx, sorted.size() - 1)];
        }
    }

    return s;
}

std::string Metrics::to_prometheus() const {
    auto s = snapshot();
    std::ostringstream out;

    out << "# HELP llm_gateway_requests_total Total requests\n";
    out << "# TYPE llm_gateway_requests_total counter\n";
    out << "llm_gateway_requests_total " << s.total_requests << "\n\n";

    out << "# HELP llm_gateway_errors_total Total errors\n";
    out << "# TYPE llm_gateway_errors_total counter\n";
    out << "llm_gateway_errors_total " << s.total_errors << "\n\n";

    out << "# HELP llm_gateway_cache_hits_total Cache hits\n";
    out << "# TYPE llm_gateway_cache_hits_total counter\n";
    out << "llm_gateway_cache_hits_total " << s.cache_hits << "\n\n";

    out << "# HELP llm_gateway_cache_misses_total Cache misses\n";
    out << "# TYPE llm_gateway_cache_misses_total counter\n";
    out << "llm_gateway_cache_misses_total " << s.cache_misses << "\n\n";

    out << "# HELP llm_gateway_latency_avg_ms Average latency in ms\n";
    out << "# TYPE llm_gateway_latency_avg_ms gauge\n";
    out << "llm_gateway_latency_avg_ms " << s.avg_latency_ms << "\n\n";

    out << "# HELP llm_gateway_latency_p99_ms P99 latency in ms\n";
    out << "# TYPE llm_gateway_latency_p99_ms gauge\n";
    out << "llm_gateway_latency_p99_ms " << s.p99_latency_ms << "\n\n";

    out << "# HELP llm_gateway_tokens_total Total tokens processed\n";
    out << "# TYPE llm_gateway_tokens_total counter\n";
    out << "llm_gateway_tokens_total " << s.total_tokens << "\n\n";

    out << "# HELP llm_gateway_uptime_seconds Uptime\n";
    out << "# TYPE llm_gateway_uptime_seconds gauge\n";
    out << "llm_gateway_uptime_seconds " << s.uptime_seconds << "\n";

    return out.str();
}

json Metrics::to_json() const {
    auto s = snapshot();
    return {
        {"total_requests",  s.total_requests},
        {"total_errors",    s.total_errors},
        {"cache_hits",      s.cache_hits},
        {"cache_misses",    s.cache_misses},
        {"avg_latency_ms",  s.avg_latency_ms},
        {"p99_latency_ms",  s.p99_latency_ms},
        {"total_tokens",    s.total_tokens},
        {"uptime_seconds",  s.uptime_seconds},
        {"cache_hit_rate",  (s.cache_hits + s.cache_misses) > 0
            ? (double)s.cache_hits / (s.cache_hits + s.cache_misses) : 0.0}
    };
}

} // namespace gateway

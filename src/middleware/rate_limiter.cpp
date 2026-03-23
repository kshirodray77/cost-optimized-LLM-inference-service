#include "middleware/rate_limiter.h"
#include "config.h"
#include <spdlog/spdlog.h>

namespace gateway {

RateLimiter& RateLimiter::instance() {
    static RateLimiter inst;
    return inst;
}

RateLimiter::Result RateLimiter::check_request(const std::string& api_key,
                                                int estimated_tokens) {
    std::lock_guard<std::mutex> lock(mu_);
    auto& bucket = get_bucket(api_key);
    reset_if_expired(bucket);

    Result r;
    r.remaining_rpm = bucket.rpm_limit - bucket.requests_count;
    r.remaining_tpm = bucket.tpm_limit - bucket.tokens_count;

    if (bucket.requests_count >= bucket.rpm_limit) {
        r.allowed = false;
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            Clock::now() - bucket.window_start).count();
        r.retry_after_sec = std::max(1, 60 - static_cast<int>(elapsed));
        spdlog::debug("Rate limited key={}: {}/{} RPM",
                      api_key.substr(0, 8), bucket.requests_count, bucket.rpm_limit);
        return r;
    }

    if (estimated_tokens > 0 && bucket.tokens_count + estimated_tokens > bucket.tpm_limit) {
        r.allowed = false;
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            Clock::now() - bucket.window_start).count();
        r.retry_after_sec = std::max(1, 60 - static_cast<int>(elapsed));
        spdlog::debug("Rate limited key={}: {}/{} TPM",
                      api_key.substr(0, 8), bucket.tokens_count, bucket.tpm_limit);
        return r;
    }

    bucket.requests_count++;
    r.allowed = true;
    return r;
}

void RateLimiter::record_tokens(const std::string& api_key, int tokens) {
    std::lock_guard<std::mutex> lock(mu_);
    auto& bucket = get_bucket(api_key);
    bucket.tokens_count += tokens;
}

void RateLimiter::reset(const std::string& api_key) {
    std::lock_guard<std::mutex> lock(mu_);
    buckets_.erase(api_key);
}

RateLimiter::Bucket& RateLimiter::get_bucket(const std::string& api_key) {
    auto it = buckets_.find(api_key);
    if (it == buckets_.end()) {
        auto& cfg = Config::instance().get().rate_limit;
        Bucket b;
        b.rpm_limit = cfg.default_rpm;
        b.tpm_limit = cfg.default_tpm;
        buckets_[api_key] = b;
        return buckets_[api_key];
    }
    return it->second;
}

void RateLimiter::reset_if_expired(Bucket& bucket) {
    auto now = Clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - bucket.window_start).count();

    if (elapsed >= 60) {
        bucket.requests_count = 0;
        bucket.tokens_count   = 0;
        bucket.window_start   = now;
    }
}

} // namespace gateway

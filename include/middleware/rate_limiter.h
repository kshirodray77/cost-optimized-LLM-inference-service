#pragma once
#include "types.h"
#include <mutex>

namespace gateway {

class RateLimiter {
public:
    static RateLimiter& instance();

    struct Result {
        bool allowed         = true;
        int  remaining_rpm   = 0;
        int  remaining_tpm   = 0;
        int  retry_after_sec = 0;
    };

    // Check if a request is allowed
    Result check_request(const std::string& api_key, int estimated_tokens = 0);

    // Record token usage after response
    void record_tokens(const std::string& api_key, int tokens);

    // Reset for a key
    void reset(const std::string& api_key);

private:
    RateLimiter() = default;

    struct Bucket {
        int    rpm_limit     = 60;
        int    tpm_limit     = 100000;
        int    requests_count = 0;
        int    tokens_count   = 0;
        TimePoint window_start = Clock::now();
    };

    mutable std::mutex mu_;
    std::unordered_map<std::string, Bucket> buckets_;

    Bucket& get_bucket(const std::string& api_key);
    void reset_if_expired(Bucket& bucket);
};

} // namespace gateway

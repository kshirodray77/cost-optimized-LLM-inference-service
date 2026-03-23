#include <gtest/gtest.h>
#include "middleware/rate_limiter.h"
#include "config.h"

using namespace gateway;

class RateLimiterTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto& cfg = Config::instance().get_mut();
        cfg.rate_limit.default_rpm = 5;
        cfg.rate_limit.default_tpm = 1000;
        RateLimiter::instance().reset("test-key");
    }
};

TEST_F(RateLimiterTest, AllowsWithinLimit) {
    auto r = RateLimiter::instance().check_request("test-key");
    EXPECT_TRUE(r.allowed);
}

TEST_F(RateLimiterTest, BlocksAfterRpmExceeded) {
    for (int i = 0; i < 5; ++i) {
        auto r = RateLimiter::instance().check_request("test-key");
        EXPECT_TRUE(r.allowed);
    }

    auto r = RateLimiter::instance().check_request("test-key");
    EXPECT_FALSE(r.allowed);
    EXPECT_GT(r.retry_after_sec, 0);
}

TEST_F(RateLimiterTest, DifferentKeysIndependent) {
    for (int i = 0; i < 5; ++i) {
        RateLimiter::instance().check_request("key-A");
    }
    // key-A is exhausted
    auto rA = RateLimiter::instance().check_request("key-A");
    EXPECT_FALSE(rA.allowed);

    // key-B should still work
    auto rB = RateLimiter::instance().check_request("key-B");
    EXPECT_TRUE(rB.allowed);
}

TEST_F(RateLimiterTest, TokenTracking) {
    RateLimiter::instance().record_tokens("test-key", 500);
    RateLimiter::instance().record_tokens("test-key", 400);

    // Next request with 200 tokens would exceed 1000 TPM limit
    auto r = RateLimiter::instance().check_request("test-key", 200);
    EXPECT_FALSE(r.allowed);
}

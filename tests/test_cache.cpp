#include <gtest/gtest.h>
#include "cache.h"
#include "config.h"

using namespace gateway;

class CacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto& cfg = Config::instance().get_mut();
        cfg.cache.enabled = true;
        cfg.cache.max_entries = 100;
        cfg.cache.ttl_seconds = 60;
        Cache::instance().clear();
    }
};

TEST_F(CacheTest, MissOnEmpty) {
    ChatRequest req;
    req.model = "gpt-4o";
    req.messages = {{"user", "Hello"}};

    auto result = Cache::instance().get(req);
    EXPECT_FALSE(result.has_value());
}

TEST_F(CacheTest, HitAfterPut) {
    ChatRequest req;
    req.model = "gpt-4o";
    req.messages = {{"user", "Hello"}};

    ChatResponse resp;
    resp.content = "Hi there!";
    resp.provider = "openai";
    resp.model = "gpt-4o";
    resp.prompt_tokens = 10;
    resp.completion_tokens = 5;
    resp.total_tokens = 15;
    resp.cost_usd = 0.001;

    Cache::instance().put(req, resp);

    auto result = Cache::instance().get(req);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->content, "Hi there!");
    EXPECT_TRUE(result->from_cache);
}

TEST_F(CacheTest, DifferentModelsMiss) {
    ChatRequest req1;
    req1.model = "gpt-4o";
    req1.messages = {{"user", "Hello"}};

    ChatResponse resp;
    resp.content = "Response";
    Cache::instance().put(req1, resp);

    ChatRequest req2;
    req2.model = "claude";
    req2.messages = {{"user", "Hello"}};

    auto result = Cache::instance().get(req2);
    EXPECT_FALSE(result.has_value());
}

TEST_F(CacheTest, StatsTracking) {
    ChatRequest req;
    req.model = "test";
    req.messages = {{"user", "Test"}};

    // One miss
    Cache::instance().get(req);

    ChatResponse resp;
    resp.content = "Response";
    resp.cost_usd = 0.01;
    Cache::instance().put(req, resp);

    // One hit
    Cache::instance().get(req);

    auto stats = Cache::instance().get_stats();
    EXPECT_EQ(stats.entries, 1u);
    EXPECT_EQ(stats.hits, 1u);
    EXPECT_EQ(stats.misses, 1u);
    EXPECT_GT(stats.savings_usd, 0);
}

TEST_F(CacheTest, LRUEviction) {
    auto& cfg = Config::instance().get_mut();
    cfg.cache.max_entries = 3;
    Cache::instance().clear();

    for (int i = 0; i < 5; ++i) {
        ChatRequest req;
        req.model = "test";
        req.messages = {{"user", "msg-" + std::to_string(i)}};
        ChatResponse resp;
        resp.content = "resp-" + std::to_string(i);
        Cache::instance().put(req, resp);
    }

    // Size should be capped
    EXPECT_LE(Cache::instance().size(), 3u);
}

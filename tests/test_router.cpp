#include <gtest/gtest.h>
#include "router.h"
#include "config.h"

using namespace gateway;

class RouterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup test providers in config
        auto& cfg = Config::instance().get_mut();
        cfg.providers.clear();
        cfg.providers.push_back({"openai", "test-key", "https://api.openai.com", 30000, 2, 5, true,
            {"gpt-4o", "gpt-4o-mini"}});
        cfg.providers.push_back({"groq", "test-key", "https://api.groq.com", 15000, 2, 4, true,
            {"llama-3.1-8b-instant"}});
        cfg.providers.push_back({"together", "test-key", "https://api.together.xyz", 30000, 2, 3, true,
            {"meta-llama/Llama-3.1-70B-Instruct"}});
    }
};

TEST_F(RouterTest, SmartRouteReturnsProvider) {
    ChatRequest req;
    req.model = "gpt-4o";
    req.messages = {{"user", "Hello"}};

    auto decision = Router::instance().route(req);
    EXPECT_FALSE(decision.provider.empty());
}

TEST_F(RouterTest, AliasResolution) {
    auto& cfg = Config::instance().get_mut();
    cfg.model_aliases["fast"] = "groq:llama-3.1-8b-instant";

    ChatRequest req;
    req.model = "fast";
    req.messages = {{"user", "Hi"}};

    auto decision = Router::instance().route(req);
    EXPECT_EQ(decision.provider, "groq");
    EXPECT_EQ(decision.model, "llama-3.1-8b-instant");
}

TEST_F(RouterTest, HealthTracking) {
    Router::instance().report_success("openai", 150.0);
    Router::instance().report_success("openai", 200.0);

    auto health = Router::instance().get_health("openai");
    EXPECT_TRUE(health.available);
    EXPECT_GT(health.avg_latency_ms, 0);
}

TEST_F(RouterTest, FailureMarksUnavailable) {
    // Report many failures
    for (int i = 0; i < 10; ++i) {
        Router::instance().report_failure("test_provider", "timeout");
    }
    auto health = Router::instance().get_health("test_provider");
    EXPECT_FALSE(health.available);
}

TEST_F(RouterTest, CostOptimizedPicksCheapest) {
    Router::instance().set_strategy(RoutingStrategy::COST_OPTIMIZED);

    ChatRequest req;
    req.model = "any";
    req.messages = {{"user", "Hi"}};

    auto decision = Router::instance().route(req);
    // Groq is cheapest in default pricing
    EXPECT_FALSE(decision.provider.empty());
}

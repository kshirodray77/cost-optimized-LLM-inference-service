#include <gtest/gtest.h>
#include "cost_tracker.h"
#include "config.h"

using namespace gateway;

TEST(CostTrackerTest, EstimateCostKnownModel) {
    auto cost = CostTracker::instance().estimate_cost("openai", "gpt-4o", 1000, 500);
    EXPECT_GT(cost, 0);
}

TEST(CostTrackerTest, EstimateCostUnknownModelUsesDefault) {
    auto cost = CostTracker::instance().estimate_cost("unknown", "unknown-model", 1000, 500);
    EXPECT_GT(cost, 0);
}

TEST(CostTrackerTest, RecordAndSummary) {
    RequestMetrics m;
    m.request_id = "test-1";
    m.api_key = "key-1";
    m.provider = "openai";
    m.model = "gpt-4o";
    m.prompt_tokens = 100;
    m.completion_tokens = 50;
    m.latency_ms = 200;
    m.cost_usd = 0.005;

    CostTracker::instance().record(m);

    auto summary = CostTracker::instance().get_summary("key-1");
    EXPECT_GT(summary.total_usd, 0);
    EXPECT_GE(summary.total_requests, 1);
}

TEST(CostTrackerTest, SpendTracking) {
    RequestMetrics m;
    m.api_key = "spend-test-key";
    m.provider = "openai";
    m.model = "gpt-4o";
    m.prompt_tokens = 1000;
    m.completion_tokens = 500;
    m.cost_usd = 0.01;

    CostTracker::instance().record(m);

    double spend = CostTracker::instance().get_spend("spend-test-key");
    EXPECT_GT(spend, 0);
}

TEST(CostTrackerTest, GroqIsCheaperThanOpenAI) {
    auto groq_cost = CostTracker::instance().estimate_cost(
        "groq", "llama-3.1-8b-instant", 1000, 500);
    auto openai_cost = CostTracker::instance().estimate_cost(
        "openai", "gpt-4o", 1000, 500);

    EXPECT_LT(groq_cost, openai_cost);
}

#pragma once
// ═══════════════════════════════════════════════════════════════════════
// LLM Gateway — Core Types
// High-performance C++ gateway for multi-provider LLM inference
// ═══════════════════════════════════════════════════════════════════════

#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <functional>
#include <memory>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace gateway {

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

// ── Chat Messages ───────────────────────────────────────────────────

struct Message {
    std::string role;    // "system", "user", "assistant"
    std::string content;

    json to_json() const {
        return {{"role", role}, {"content", content}};
    }

    static Message from_json(const json& j) {
        return {j.value("role", ""), j.value("content", "")};
    }
};

// ── Inbound Request ─────────────────────────────────────────────────

struct ChatRequest {
    std::string request_id;
    std::string api_key;             // Virtual API key from header
    std::string model;               // Requested model or alias
    std::vector<Message> messages;
    double temperature       = 0.7;
    int    max_tokens        = 1024;
    bool   stream            = false;
    std::optional<std::string> user;
    json   extra;                    // Pass-through provider params
    TimePoint received_at    = Clock::now();
};

// ── Outbound Response ───────────────────────────────────────────────

struct ChatResponse {
    std::string request_id;
    std::string model;               // Actual model used
    std::string provider;            // Which provider served it
    std::string content;             // Assistant message
    int    prompt_tokens     = 0;
    int    completion_tokens = 0;
    int    total_tokens      = 0;
    double latency_ms        = 0;
    double cost_usd          = 0;
    bool   from_cache        = false;
    json   raw_response;             // Full provider response
};

// ── Streaming ───────────────────────────────────────────────────────

struct StreamChunk {
    std::string delta;
    bool done = false;
    std::optional<int> prompt_tokens;
    std::optional<int> completion_tokens;
};

using StreamCallback = std::function<void(const StreamChunk&)>;

// ── Provider Health ─────────────────────────────────────────────────

struct ProviderHealth {
    std::string name;
    bool   available             = true;
    double avg_latency_ms        = 0;
    double error_rate            = 0;    // 0.0 - 1.0
    int    requests_last_minute  = 0;
    int    errors_last_minute    = 0;
    TimePoint last_success;
    TimePoint last_error;
};

// ── Routing ─────────────────────────────────────────────────────────

enum class RoutingStrategy {
    COST_OPTIMIZED,
    LATENCY_OPTIMIZED,
    ROUND_ROBIN,
    FAILOVER,
    SMART                  // Weighted: health + cost + latency
};

inline std::string strategy_to_string(RoutingStrategy s) {
    switch (s) {
        case RoutingStrategy::COST_OPTIMIZED:    return "cost";
        case RoutingStrategy::LATENCY_OPTIMIZED: return "latency";
        case RoutingStrategy::ROUND_ROBIN:       return "round_robin";
        case RoutingStrategy::FAILOVER:          return "failover";
        case RoutingStrategy::SMART:             return "smart";
        default: return "unknown";
    }
}

struct RouteDecision {
    std::string provider;
    std::string model;
    RoutingStrategy strategy_used;
    double estimated_cost      = 0;
    std::vector<std::string> fallback_chain;
};

// ── API Keys / Auth ─────────────────────────────────────────────────

struct VirtualKey {
    std::string key_id;
    std::string key_hash;
    std::string owner;
    std::string tier = "free";       // free, pro, team, enterprise
    int    rate_limit_rpm  = 60;     // Requests per minute
    int    rate_limit_tpm  = 100000; // Tokens per minute
    double budget_usd      = 0;     // 0 = unlimited
    double spent_usd       = 0;
    bool   active          = true;
    std::vector<std::string> allowed_models;
    TimePoint created_at   = Clock::now();
};

// ── Pricing ─────────────────────────────────────────────────────────

struct ModelPricing {
    std::string provider;
    std::string model;
    double input_cost_per_1k  = 0;   // USD per 1K input tokens
    double output_cost_per_1k = 0;   // USD per 1K output tokens
};

// ── Metrics ─────────────────────────────────────────────────────────

struct RequestMetrics {
    std::string request_id;
    std::string api_key;
    std::string provider;
    std::string model;
    int    prompt_tokens     = 0;
    int    completion_tokens = 0;
    double latency_ms        = 0;
    double cost_usd          = 0;
    bool   cache_hit         = false;
    bool   success           = true;
    std::string error_message;
    TimePoint timestamp      = Clock::now();
};

} // namespace gateway

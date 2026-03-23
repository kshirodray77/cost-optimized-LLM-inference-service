#pragma once
// ═══════════════════════════════════════════════════════════════════════
// Router — Smart multi-provider routing with failover
// Replaces the original simple prompt-length router
// ═══════════════════════════════════════════════════════════════════════

#include "types.h"
#include "config.h"
#include <mutex>
#include <atomic>

namespace gateway {

class Router {
public:
    static Router& instance();

    // Core routing: picks best provider + model, returns fallback chain
    RouteDecision route(const ChatRequest& req);

    // Health tracking
    void report_success(const std::string& provider, double latency_ms);
    void report_failure(const std::string& provider, const std::string& error);
    ProviderHealth get_health(const std::string& provider) const;
    std::vector<ProviderHealth> get_all_health() const;

    // Strategy
    void set_strategy(RoutingStrategy strategy);
    RoutingStrategy get_strategy() const;

private:
    Router();
    mutable std::mutex mu_;
    std::unordered_map<std::string, ProviderHealth> health_;
    std::atomic<int> round_robin_idx_{0};
    RoutingStrategy strategy_ = RoutingStrategy::SMART;

    // Routing strategies
    RouteDecision route_cost_optimized(const ChatRequest& req);
    RouteDecision route_latency_optimized(const ChatRequest& req);
    RouteDecision route_round_robin(const ChatRequest& req);
    RouteDecision route_failover(const ChatRequest& req);
    RouteDecision route_smart(const ChatRequest& req);

    // Helpers
    std::vector<std::string> get_providers_for_model(const std::string& model) const;
    double score_provider(const std::string& provider, const ChatRequest& req) const;
    std::string resolve_model_alias(const std::string& model) const;
};

} // namespace gateway

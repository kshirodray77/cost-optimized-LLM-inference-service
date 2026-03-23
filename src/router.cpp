#include "router.h"
#include "providers/provider_registry.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace gateway {

Router& Router::instance() {
    static Router inst;
    return inst;
}

Router::Router() {
    strategy_ = Config::instance().get().default_strategy;
}

RouteDecision Router::route(const ChatRequest& req) {
    std::lock_guard<std::mutex> lock(mu_);

    // Resolve model alias (e.g. "fast" -> "groq:llama-3.1-8b-instant")
    std::string resolved = resolve_model_alias(req.model);

    // If alias resolves to provider:model, route directly
    auto colon = resolved.find(':');
    if (colon != std::string::npos) {
        std::string provider = resolved.substr(0, colon);
        std::string model    = resolved.substr(colon + 1);

        // Build fallback chain from other providers that support similar models
        std::vector<std::string> fallbacks;
        for (auto& [name, h] : health_) {
            if (name != provider && h.available) fallbacks.push_back(name);
        }

        return {provider, model, strategy_, 0, fallbacks};
    }

    // Otherwise use the configured strategy
    switch (strategy_) {
        case RoutingStrategy::COST_OPTIMIZED:    return route_cost_optimized(req);
        case RoutingStrategy::LATENCY_OPTIMIZED: return route_latency_optimized(req);
        case RoutingStrategy::ROUND_ROBIN:       return route_round_robin(req);
        case RoutingStrategy::FAILOVER:          return route_failover(req);
        case RoutingStrategy::SMART:
        default:                                 return route_smart(req);
    }
}

// ── Strategy Implementations ────────────────────────────────────────

RouteDecision Router::route_cost_optimized(const ChatRequest& req) {
    auto& cfg = Config::instance().get();
    std::string best_provider, best_model;
    double best_cost = 1e9;

    for (auto& pc : cfg.providers) {
        if (!pc.enabled) continue;
        auto it = health_.find(pc.name);
        if (it != health_.end() && !it->second.available) continue;

        for (auto& model : pc.models) {
            std::string key = pc.name + ":" + model;
            auto pit = cfg.pricing.find(key);
            if (pit != cfg.pricing.end()) {
                double cost = pit->second.input_cost_per_1k + pit->second.output_cost_per_1k;
                if (cost < best_cost) {
                    best_cost = cost;
                    best_provider = pc.name;
                    best_model = model;
                }
            }
        }
    }

    if (best_provider.empty()) {
        // Fallback: first available provider
        for (auto& pc : cfg.providers) {
            if (pc.enabled && !pc.models.empty()) {
                best_provider = pc.name;
                best_model = pc.models[0];
                break;
            }
        }
    }

    return {best_provider, best_model, RoutingStrategy::COST_OPTIMIZED, best_cost, {}};
}

RouteDecision Router::route_latency_optimized(const ChatRequest& req) {
    std::string best_provider, best_model;
    double best_latency = 1e9;
    auto& cfg = Config::instance().get();

    for (auto& pc : cfg.providers) {
        if (!pc.enabled) continue;
        auto it = health_.find(pc.name);
        double latency = (it != health_.end()) ? it->second.avg_latency_ms : 500.0;

        if (it != health_.end() && !it->second.available) continue;

        if (latency < best_latency && !pc.models.empty()) {
            best_latency = latency;
            best_provider = pc.name;
            best_model = pc.models[0];
        }
    }

    return {best_provider, best_model, RoutingStrategy::LATENCY_OPTIMIZED, 0, {}};
}

RouteDecision Router::route_round_robin(const ChatRequest& req) {
    auto& cfg = Config::instance().get();
    std::vector<const ProviderConfig*> available;
    for (auto& pc : cfg.providers) {
        if (!pc.enabled || pc.models.empty()) continue;
        auto it = health_.find(pc.name);
        if (it == health_.end() || it->second.available)
            available.push_back(&pc);
    }

    if (available.empty()) return {"", "", RoutingStrategy::ROUND_ROBIN, 0, {}};

    int idx = round_robin_idx_.fetch_add(1) % available.size();
    auto* pc = available[idx];
    return {pc->name, pc->models[0], RoutingStrategy::ROUND_ROBIN, 0, {}};
}

RouteDecision Router::route_failover(const ChatRequest& req) {
    auto& cfg = Config::instance().get();
    std::vector<std::string> chain;

    for (auto& pc : cfg.providers) {
        if (!pc.enabled || pc.models.empty()) continue;
        auto it = health_.find(pc.name);
        if (it == health_.end() || it->second.available) {
            chain.push_back(pc.name);
        }
    }

    if (chain.empty()) return {"", "", RoutingStrategy::FAILOVER, 0, {}};

    std::string primary = chain[0];
    chain.erase(chain.begin());

    std::string model;
    for (auto& pc : cfg.providers) {
        if (pc.name == primary && !pc.models.empty()) {
            model = pc.models[0];
            break;
        }
    }

    return {primary, model, RoutingStrategy::FAILOVER, 0, chain};
}

RouteDecision Router::route_smart(const ChatRequest& req) {
    auto& cfg = Config::instance().get();

    struct Candidate {
        std::string provider;
        std::string model;
        double score;
    };
    std::vector<Candidate> candidates;

    for (auto& pc : cfg.providers) {
        if (!pc.enabled) continue;
        auto it = health_.find(pc.name);
        if (it != health_.end() && !it->second.available) continue;

        for (auto& model : pc.models) {
            double score = score_provider(pc.name, req);

            // Cost factor (lower is better)
            std::string key = pc.name + ":" + model;
            auto pit = cfg.pricing.find(key);
            if (pit != cfg.pricing.end()) {
                double cost = pit->second.input_cost_per_1k + pit->second.output_cost_per_1k;
                score -= cost * 100;  // Penalize expensive models
            }

            candidates.push_back({pc.name, model, score});
        }
    }

    if (candidates.empty()) return {"", "", RoutingStrategy::SMART, 0, {}};

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

    std::vector<std::string> fallbacks;
    for (size_t i = 1; i < candidates.size() && i < 4; ++i)
        fallbacks.push_back(candidates[i].provider);

    return {candidates[0].provider, candidates[0].model,
            RoutingStrategy::SMART, 0, fallbacks};
}

// ── Health Tracking ─────────────────────────────────────────────────

void Router::report_success(const std::string& provider, double latency_ms) {
    std::lock_guard<std::mutex> lock(mu_);
    auto& h = health_[provider];
    h.name = provider;
    h.available = true;
    h.requests_last_minute++;
    h.last_success = Clock::now();

    // Exponential moving average for latency
    if (h.avg_latency_ms == 0)
        h.avg_latency_ms = latency_ms;
    else
        h.avg_latency_ms = h.avg_latency_ms * 0.9 + latency_ms * 0.1;

    // Decay error rate
    h.error_rate = h.error_rate * 0.95;
}

void Router::report_failure(const std::string& provider, const std::string& error) {
    std::lock_guard<std::mutex> lock(mu_);
    auto& h = health_[provider];
    h.name = provider;
    h.errors_last_minute++;
    h.last_error = Clock::now();
    h.error_rate = std::min(1.0, h.error_rate + 0.1);

    // Mark unavailable if error rate too high
    if (h.error_rate > 0.5) {
        h.available = false;
        spdlog::warn("Provider {} marked unavailable (error_rate={:.2f})", provider, h.error_rate);
    }
}

ProviderHealth Router::get_health(const std::string& provider) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = health_.find(provider);
    if (it != health_.end()) return it->second;
    return {provider, true, 0, 0, 0, 0, {}, {}};
}

std::vector<ProviderHealth> Router::get_all_health() const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<ProviderHealth> result;
    for (auto& [_, h] : health_) result.push_back(h);
    return result;
}

void Router::set_strategy(RoutingStrategy s) { strategy_ = s; }
RoutingStrategy Router::get_strategy() const { return strategy_; }

// ── Helpers ─────────────────────────────────────────────────────────

double Router::score_provider(const std::string& provider, const ChatRequest& req) const {
    double score = 100.0;

    auto it = health_.find(provider);
    if (it != health_.end()) {
        const auto& h = it->second;
        score -= h.error_rate * 50;                    // Penalize errors
        score -= std::min(h.avg_latency_ms / 100.0, 30.0); // Penalize slow
        if (!h.available) score -= 1000;               // Effectively disable
    }

    // Weight from config
    auto& cfg = Config::instance().get();
    for (auto& pc : cfg.providers) {
        if (pc.name == provider) {
            score += pc.weight * 10;
            break;
        }
    }

    return score;
}

std::string Router::resolve_model_alias(const std::string& model) const {
    auto& aliases = Config::instance().get().model_aliases;
    auto it = aliases.find(model);
    return (it != aliases.end()) ? it->second : model;
}

} // namespace gateway

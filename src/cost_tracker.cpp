#include "cost_tracker.h"
#include "config.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace gateway {

CostTracker& CostTracker::instance() {
    static CostTracker inst;
    return inst;
}

CostTracker::CostTracker() {
    // Load pricing from config
    for (auto& [key, p] : Config::instance().get().pricing) {
        pricing_[key] = p;
    }
}

double CostTracker::estimate_cost(const std::string& provider, const std::string& model,
                                   int input_tokens, int estimated_output_tokens) const {
    std::lock_guard<std::mutex> lock(mu_);
    std::string key = provider + ":" + model;
    auto it = pricing_.find(key);
    if (it == pricing_.end()) {
        // Default fallback pricing
        return (input_tokens + estimated_output_tokens) * 0.002 / 1000.0;
    }

    double in_cost  = (input_tokens / 1000.0) * it->second.input_cost_per_1k;
    double out_cost = (estimated_output_tokens / 1000.0) * it->second.output_cost_per_1k;
    return in_cost + out_cost;
}

void CostTracker::record(const RequestMetrics& metrics) {
    std::lock_guard<std::mutex> lock(mu_);

    // Calculate actual cost
    RequestMetrics m = metrics;
    std::string key = m.provider + ":" + m.model;
    auto it = pricing_.find(key);
    if (it != pricing_.end()) {
        double in_cost  = (m.prompt_tokens / 1000.0) * it->second.input_cost_per_1k;
        double out_cost = (m.completion_tokens / 1000.0) * it->second.output_cost_per_1k;
        m.cost_usd = in_cost + out_cost;
    }

    // Track spend per key
    if (!m.api_key.empty()) {
        spend_by_key_[m.api_key] += m.cost_usd;
    }

    // Store history (ring buffer)
    if (history_.size() >= MAX_HISTORY) {
        history_.erase(history_.begin());
    }
    history_.push_back(m);

    spdlog::debug("Cost recorded: {} {} {}tok -> ${:.6f}",
                  m.provider, m.model, m.prompt_tokens + m.completion_tokens, m.cost_usd);
}

bool CostTracker::check_budget(const std::string& api_key) const {
    // TODO: Check against VirtualKey budget
    // For now, no budget enforcement
    return true;
}

double CostTracker::get_spend(const std::string& api_key) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = spend_by_key_.find(api_key);
    return (it != spend_by_key_.end()) ? it->second : 0.0;
}

CostTracker::SpendSummary CostTracker::get_summary(const std::string& api_key) const {
    std::lock_guard<std::mutex> lock(mu_);
    SpendSummary s;

    for (auto& m : history_) {
        if (!api_key.empty() && m.api_key != api_key) continue;

        s.total_usd += m.cost_usd;
        s.total_requests++;
        if (m.cache_hit) {
            s.cached_requests++;
            s.cache_savings_usd += m.cost_usd;
        }
        s.by_provider[m.provider] += m.cost_usd;
        s.by_model[m.model] += m.cost_usd;
    }

    return s;
}

std::vector<RequestMetrics> CostTracker::get_recent(int limit) const {
    std::lock_guard<std::mutex> lock(mu_);
    int start = std::max(0, static_cast<int>(history_.size()) - limit);
    return {history_.begin() + start, history_.end()};
}

void CostTracker::set_pricing(const std::string& model, const ModelPricing& pricing) {
    std::lock_guard<std::mutex> lock(mu_);
    std::string key = pricing.provider + ":" + model;
    pricing_[key] = pricing;
}

ModelPricing CostTracker::get_pricing(const std::string& provider,
                                       const std::string& model) const {
    std::lock_guard<std::mutex> lock(mu_);
    std::string key = provider + ":" + model;
    auto it = pricing_.find(key);
    if (it != pricing_.end()) return it->second;
    return {provider, model, 0.002, 0.002};  // Default
}

} // namespace gateway

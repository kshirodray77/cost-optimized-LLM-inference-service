#pragma once
// ═══════════════════════════════════════════════════════════════════════
// Cost Tracker — Per-model pricing, spend tracking, budget enforcement
// Replaces the original single-function token counter
// ═══════════════════════════════════════════════════════════════════════

#include "types.h"
#include <mutex>

namespace gateway {

class CostTracker {
public:
    static CostTracker& instance();

    // Estimate cost BEFORE making the call (for routing decisions)
    double estimate_cost(const std::string& provider, const std::string& model,
                         int input_tokens, int estimated_output_tokens) const;

    // Record actual cost AFTER the call
    void record(const RequestMetrics& metrics);

    // Budget enforcement
    bool check_budget(const std::string& api_key) const;
    double get_spend(const std::string& api_key) const;

    // Analytics
    struct SpendSummary {
        double total_usd          = 0;
        double cache_savings_usd  = 0;
        int    total_requests     = 0;
        int    cached_requests    = 0;
        std::unordered_map<std::string, double> by_provider;
        std::unordered_map<std::string, double> by_model;
    };

    SpendSummary get_summary(const std::string& api_key = "") const;
    std::vector<RequestMetrics> get_recent(int limit = 100) const;

    // Pricing management
    void set_pricing(const std::string& model, const ModelPricing& pricing);
    ModelPricing get_pricing(const std::string& provider,
                             const std::string& model) const;

private:
    CostTracker();
    mutable std::mutex mu_;
    std::vector<RequestMetrics> history_;
    std::unordered_map<std::string, double> spend_by_key_;
    std::unordered_map<std::string, ModelPricing> pricing_;

    static constexpr size_t MAX_HISTORY = 100000;
};

} // namespace gateway

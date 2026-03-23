#pragma once
#include "types.h"
#include <mutex>

namespace gateway {

struct ProviderConfig {
    std::string name;
    std::string api_key;
    std::string base_url;
    int  timeout_ms   = 30000;
    int  max_retries  = 2;
    int  weight       = 1;
    bool enabled      = true;
    std::vector<std::string> models;
};

struct CacheConfig {
    bool enabled       = true;
    int  ttl_seconds   = 3600;
    int  max_entries   = 10000;
    bool match_params  = true;  // Include temp/max_tokens in cache key
};

struct ServerConfig {
    std::string host      = "0.0.0.0";
    int  port             = 8080;
    int  threads          = 4;
    bool enable_cors      = true;
    std::string log_level = "info";
    std::string admin_key;
};

struct RateLimitConfig {
    int default_rpm = 60;
    int default_tpm = 100000;
};

struct GatewayConfig {
    ServerConfig   server;
    CacheConfig    cache;
    RateLimitConfig rate_limit;
    RoutingStrategy default_strategy = RoutingStrategy::SMART;
    std::vector<ProviderConfig> providers;
    std::unordered_map<std::string, ModelPricing> pricing;
    std::unordered_map<std::string, std::string>  model_aliases;
};

class Config {
public:
    static Config& instance();

    bool load_from_file(const std::string& path);
    bool load_from_env();

    const GatewayConfig& get() const { return config_; }
    GatewayConfig& get_mut() { return config_; }

private:
    Config() { set_default_pricing(); }
    GatewayConfig config_;
    mutable std::mutex mu_;

    void set_default_pricing();
    void apply_env_overrides();
};

} // namespace gateway

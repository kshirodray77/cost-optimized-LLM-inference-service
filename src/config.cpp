#include "config.h"
#include <fstream>
#include <spdlog/spdlog.h>

namespace gateway {

Config& Config::instance() {
    static Config inst;
    return inst;
}

bool Config::load_from_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        spdlog::warn("Config file not found: {}, using defaults + env vars", path);
        apply_env_overrides();
        return false;
    }

    try {
        json j = json::parse(f);

        // Server
        if (j.contains("server")) {
            auto& s = j["server"];
            config_.server.host      = s.value("host", config_.server.host);
            config_.server.port      = s.value("port", config_.server.port);
            config_.server.threads   = s.value("threads", config_.server.threads);
            config_.server.log_level = s.value("log_level", config_.server.log_level);
            config_.server.admin_key = s.value("admin_key", config_.server.admin_key);
            config_.server.enable_cors = s.value("enable_cors", config_.server.enable_cors);
        }

        // Cache
        if (j.contains("cache")) {
            auto& c = j["cache"];
            config_.cache.enabled     = c.value("enabled", config_.cache.enabled);
            config_.cache.ttl_seconds = c.value("ttl_seconds", config_.cache.ttl_seconds);
            config_.cache.max_entries = c.value("max_entries", config_.cache.max_entries);
            config_.cache.match_params = c.value("match_params", config_.cache.match_params);
        }

        // Rate limits
        if (j.contains("rate_limit")) {
            auto& r = j["rate_limit"];
            config_.rate_limit.default_rpm = r.value("default_rpm", config_.rate_limit.default_rpm);
            config_.rate_limit.default_tpm = r.value("default_tpm", config_.rate_limit.default_tpm);
        }

        // Routing strategy
        if (j.contains("routing_strategy")) {
            std::string s = j["routing_strategy"];
            if (s == "cost")         config_.default_strategy = RoutingStrategy::COST_OPTIMIZED;
            else if (s == "latency") config_.default_strategy = RoutingStrategy::LATENCY_OPTIMIZED;
            else if (s == "round_robin") config_.default_strategy = RoutingStrategy::ROUND_ROBIN;
            else if (s == "failover")    config_.default_strategy = RoutingStrategy::FAILOVER;
            else                         config_.default_strategy = RoutingStrategy::SMART;
        }

        // Providers
        if (j.contains("providers") && j["providers"].is_array()) {
            config_.providers.clear();
            for (auto& p : j["providers"]) {
                ProviderConfig pc;
                pc.name       = p.value("name", "");
                pc.api_key    = p.value("api_key", "");
                pc.base_url   = p.value("base_url", "");
                pc.timeout_ms = p.value("timeout_ms", 30000);
                pc.max_retries = p.value("max_retries", 2);
                pc.weight     = p.value("weight", 1);
                pc.enabled    = p.value("enabled", true);
                if (p.contains("models") && p["models"].is_array()) {
                    for (auto& m : p["models"]) pc.models.push_back(m.get<std::string>());
                }
                config_.providers.push_back(std::move(pc));
            }
        }

        // Model aliases
        if (j.contains("model_aliases") && j["model_aliases"].is_object()) {
            for (auto& [alias, target] : j["model_aliases"].items()) {
                config_.model_aliases[alias] = target.get<std::string>();
            }
        }

        spdlog::info("Config loaded from {}", path);
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse config: {}", e.what());
        return false;
    }

    apply_env_overrides();
    return true;
}

bool Config::load_from_env() {
    apply_env_overrides();
    return true;
}

void Config::apply_env_overrides() {
    // Environment variables override config file values
    auto env = [](const char* name) -> std::optional<std::string> {
        const char* v = std::getenv(name);
        return v ? std::optional<std::string>(v) : std::nullopt;
    };

    if (auto v = env("LLM_GATEWAY_PORT"))      config_.server.port = std::stoi(*v);
    if (auto v = env("LLM_GATEWAY_HOST"))       config_.server.host = *v;
    if (auto v = env("LLM_GATEWAY_THREADS"))    config_.server.threads = std::stoi(*v);
    if (auto v = env("LLM_GATEWAY_LOG_LEVEL"))  config_.server.log_level = *v;
    if (auto v = env("LLM_GATEWAY_ADMIN_KEY"))  config_.server.admin_key = *v;

    // Provider API keys from env (highest priority)
    if (auto v = env("OPENAI_API_KEY")) {
        for (auto& p : config_.providers)
            if (p.name == "openai") p.api_key = *v;
    }
    if (auto v = env("ANTHROPIC_API_KEY")) {
        for (auto& p : config_.providers)
            if (p.name == "anthropic") p.api_key = *v;
    }
    if (auto v = env("TOGETHER_API_KEY")) {
        for (auto& p : config_.providers)
            if (p.name == "together") p.api_key = *v;
    }
    if (auto v = env("GROQ_API_KEY")) {
        for (auto& p : config_.providers)
            if (p.name == "groq") p.api_key = *v;
    }

    spdlog::debug("Environment overrides applied");
}

void Config::set_default_pricing() {
    // 2025/2026 pricing (USD per 1K tokens)
    auto add = [&](const std::string& provider, const std::string& model,
                   double in_1k, double out_1k) {
        std::string key = provider + ":" + model;
        config_.pricing[key] = {provider, model, in_1k, out_1k};
    };

    // OpenAI
    add("openai", "gpt-4o",          0.0025,  0.010);
    add("openai", "gpt-4o-mini",     0.00015, 0.0006);
    add("openai", "gpt-4-turbo",     0.01,    0.03);
    add("openai", "o1",              0.015,   0.06);
    add("openai", "o1-mini",         0.003,   0.012);
    add("openai", "o3-mini",         0.0011,  0.0044);

    // Anthropic
    add("anthropic", "claude-sonnet-4-20250514", 0.003, 0.015);
    add("anthropic", "claude-haiku-3-5",         0.0008, 0.004);
    add("anthropic", "claude-opus-4",            0.015, 0.075);

    // Together AI (much cheaper)
    add("together", "meta-llama/Llama-3.1-8B-Instruct",   0.00018, 0.00018);
    add("together", "meta-llama/Llama-3.1-70B-Instruct",  0.00088, 0.00088);
    add("together", "mistralai/Mixtral-8x7B-Instruct-v0.1", 0.0006, 0.0006);

    // Groq (fastest, competitive pricing)
    add("groq", "llama-3.1-8b-instant",  0.00005, 0.00008);
    add("groq", "llama-3.1-70b-versatile", 0.00059, 0.00079);
    add("groq", "mixtral-8x7b-32768",     0.00024, 0.00024);

    // Default aliases
    config_.model_aliases["gpt-4o"]      = "openai:gpt-4o";
    config_.model_aliases["claude"]      = "anthropic:claude-sonnet-4-20250514";
    config_.model_aliases["llama-70b"]   = "together:meta-llama/Llama-3.1-70B-Instruct";
    config_.model_aliases["fast"]        = "groq:llama-3.1-8b-instant";
    config_.model_aliases["cheap"]       = "together:meta-llama/Llama-3.1-8B-Instruct";
    config_.model_aliases["smart"]       = "anthropic:claude-sonnet-4-20250514";
}

} // namespace gateway

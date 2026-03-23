#include "api/endpoints.h"
#include "router.h"
#include "cache.h"
#include "utils/metrics.h"
#include "providers/provider_registry.h"
#include <nlohmann/json.hpp>

namespace gateway { namespace api {

using json = nlohmann::json;

void register_health(crow::SimpleApp& app) {

    // ── Health check ────────────────────────────────────────────────
    CROW_ROUTE(app, "/health")
    ([]() {
        json result = {
            {"status", "ok"},
            {"version", "1.0.0"},
            {"providers", json::object()}
        };

        auto providers = ProviderRegistry::instance().available_providers();
        for (auto& name : providers) {
            auto health = Router::instance().get_health(name);
            result["providers"][name] = {
                {"available", health.available},
                {"avg_latency_ms", health.avg_latency_ms},
                {"error_rate", health.error_rate}
            };
        }

        crow::response resp(200);
        resp.set_header("Content-Type", "application/json");
        resp.body = result.dump();
        return resp;
    });

    // ── Prometheus metrics ──────────────────────────────────────────
    CROW_ROUTE(app, "/metrics")
    ([]() {
        crow::response resp(200);
        resp.set_header("Content-Type", "text/plain; version=0.0.4; charset=utf-8");
        resp.body = Metrics::instance().to_prometheus();
        return resp;
    });

    // ── Detailed stats (JSON) ───────────────────────────────────────
    CROW_ROUTE(app, "/v1/stats")
    ([]() {
        auto& metrics = Metrics::instance();
        auto cache_stats = Cache::instance().get_stats();

        json result = {
            {"metrics", metrics.to_json()},
            {"cache", {
                {"entries", cache_stats.entries},
                {"max_size", cache_stats.max_size},
                {"hits", cache_stats.hits},
                {"misses", cache_stats.misses},
                {"evictions", cache_stats.evictions},
                {"hit_rate", (cache_stats.hits + cache_stats.misses) > 0
                    ? (double)cache_stats.hits / (cache_stats.hits + cache_stats.misses) : 0.0},
                {"savings_usd", cache_stats.savings_usd}
            }},
            {"providers", json::object()}
        };

        for (auto& name : ProviderRegistry::instance().available_providers()) {
            auto h = Router::instance().get_health(name);
            result["providers"][name] = {
                {"available", h.available},
                {"avg_latency_ms", h.avg_latency_ms},
                {"error_rate", h.error_rate},
                {"requests_last_minute", h.requests_last_minute},
                {"errors_last_minute", h.errors_last_minute}
            };
        }

        crow::response resp(200);
        resp.set_header("Content-Type", "application/json");
        resp.body = result.dump(2);
        return resp;
    });

    // ── Models list (OpenAI compatible) ─────────────────────────────
    CROW_ROUTE(app, "/v1/models")
    ([]() {
        json models = json::array();
        for (auto& name : ProviderRegistry::instance().available_providers()) {
            auto provider = ProviderRegistry::instance().get(name);
            if (!provider) continue;
            for (auto& model : provider->supported_models()) {
                models.push_back({
                    {"id", model},
                    {"object", "model"},
                    {"owned_by", name},
                    {"created", 0}
                });
            }
        }

        json result = {{"object", "list"}, {"data", models}};
        crow::response resp(200);
        resp.set_header("Content-Type", "application/json");
        resp.body = result.dump();
        return resp;
    });
}

}} // namespace gateway::api

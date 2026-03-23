#include "api/endpoints.h"
#include "middleware/auth.h"
#include "router.h"
#include "cache.h"
#include "cost_tracker.h"
#include "config.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace gateway { namespace api {

using json = nlohmann::json;

static crow::response admin_error(const std::string& msg) {
    json err = {{"error", msg}};
    crow::response resp(403);
    resp.set_header("Content-Type", "application/json");
    resp.body = err.dump();
    return resp;
}

void register_admin(crow::SimpleApp& app) {

    // ── Create API key ──────────────────────────────────────────────
    CROW_ROUTE(app, "/admin/keys").methods("POST"_method)
    ([](const crow::request& req) {
        std::string auth = std::string(req.get_header_value("Authorization"));
        if (!AuthMiddleware::instance().is_admin(auth)) {
            return admin_error("Admin key required");
        }

        json body;
        try { body = json::parse(req.body); } catch (...) {
            json err = {{"error", "Invalid JSON"}};
            crow::response resp(400);
            resp.set_header("Content-Type", "application/json");
            resp.body = err.dump();
            return resp;
        }

        std::string owner = body.value("owner", "anonymous");
        std::string tier  = body.value("tier", "free");

        auto key = AuthMiddleware::instance().create_key(owner, tier);

        json result = {
            {"key_id", key.key_id},
            {"api_key", key.key_hash},  // Raw key - only shown once
            {"owner", key.owner},
            {"tier", key.tier},
            {"rate_limit_rpm", key.rate_limit_rpm},
            {"rate_limit_tpm", key.rate_limit_tpm},
            {"budget_usd", key.budget_usd},
            {"message", "Save this API key - it will not be shown again"}
        };

        crow::response resp(201);
        resp.set_header("Content-Type", "application/json");
        resp.body = result.dump(2);
        return resp;
    });

    // ── List API keys ───────────────────────────────────────────────
    CROW_ROUTE(app, "/admin/keys").methods("GET"_method)
    ([](const crow::request& req) {
        std::string auth = std::string(req.get_header_value("Authorization"));
        if (!AuthMiddleware::instance().is_admin(auth)) {
            return admin_error("Admin key required");
        }

        auto keys = AuthMiddleware::instance().list_keys();
        json result = json::array();
        for (auto& k : keys) {
            result.push_back({
                {"key_id", k.key_id},
                {"owner", k.owner},
                {"tier", k.tier},
                {"active", k.active},
                {"rate_limit_rpm", k.rate_limit_rpm},
                {"spent_usd", k.spent_usd}
            });
        }

        crow::response resp(200);
        resp.set_header("Content-Type", "application/json");
        resp.body = result.dump(2);
        return resp;
    });

    // ── Revoke API key ──────────────────────────────────────────────
    CROW_ROUTE(app, "/admin/keys/<string>").methods("DELETE"_method)
    ([](const crow::request& req, std::string key_id) {
        std::string auth = std::string(req.get_header_value("Authorization"));
        if (!AuthMiddleware::instance().is_admin(auth)) {
            return admin_error("Admin key required");
        }

        bool ok = AuthMiddleware::instance().revoke_key(key_id);
        json result = {{"revoked", ok}, {"key_id", key_id}};

        crow::response resp(ok ? 200 : 404);
        resp.set_header("Content-Type", "application/json");
        resp.body = result.dump();
        return resp;
    });

    // ── Set routing strategy ────────────────────────────────────────
    CROW_ROUTE(app, "/admin/routing").methods("POST"_method)
    ([](const crow::request& req) {
        std::string auth = std::string(req.get_header_value("Authorization"));
        if (!AuthMiddleware::instance().is_admin(auth)) {
            return admin_error("Admin key required");
        }

        json body;
        try { body = json::parse(req.body); } catch (...) {
            json err = {{"error", "Invalid JSON"}};
            crow::response resp(400);
            resp.set_header("Content-Type", "application/json");
            resp.body = err.dump();
            return resp;
        }

        std::string strategy = body.value("strategy", "smart");
        RoutingStrategy s = RoutingStrategy::SMART;
        if (strategy == "cost")         s = RoutingStrategy::COST_OPTIMIZED;
        else if (strategy == "latency") s = RoutingStrategy::LATENCY_OPTIMIZED;
        else if (strategy == "round_robin") s = RoutingStrategy::ROUND_ROBIN;
        else if (strategy == "failover")    s = RoutingStrategy::FAILOVER;

        Router::instance().set_strategy(s);

        json result = {{"strategy", strategy_to_string(s)}};
        crow::response resp(200);
        resp.set_header("Content-Type", "application/json");
        resp.body = result.dump();
        return resp;
    });

    // ── Cache management ────────────────────────────────────────────
    CROW_ROUTE(app, "/admin/cache").methods("DELETE"_method)
    ([](const crow::request& req) {
        std::string auth = std::string(req.get_header_value("Authorization"));
        if (!AuthMiddleware::instance().is_admin(auth)) {
            return admin_error("Admin key required");
        }

        Cache::instance().clear();
        json result = {{"cleared", true}};
        crow::response resp(200);
        resp.set_header("Content-Type", "application/json");
        resp.body = result.dump();
        return resp;
    });

    // ── Cost summary ────────────────────────────────────────────────
    CROW_ROUTE(app, "/admin/costs")
    ([](const crow::request& req) {
        std::string auth = std::string(req.get_header_value("Authorization"));
        if (!AuthMiddleware::instance().is_admin(auth)) {
            return admin_error("Admin key required");
        }

        auto summary = CostTracker::instance().get_summary();
        json result = {
            {"total_usd", summary.total_usd},
            {"cache_savings_usd", summary.cache_savings_usd},
            {"total_requests", summary.total_requests},
            {"cached_requests", summary.cached_requests},
            {"by_provider", summary.by_provider},
            {"by_model", summary.by_model}
        };

        crow::response resp(200);
        resp.set_header("Content-Type", "application/json");
        resp.body = result.dump(2);
        return resp;
    });
}

}} // namespace gateway::api

#include "api/endpoints.h"
#include "types.h"
#include "router.h"
#include "cache.h"
#include "cost_tracker.h"
#include "providers/provider_registry.h"
#include "middleware/auth.h"
#include "middleware/rate_limiter.h"
#include "middleware/logger.h"
#include "utils/token_counter.h"
#include "utils/metrics.h"
#include <spdlog/spdlog.h>
#include <random>
#include <sstream>

namespace gateway { namespace api {

static std::string generate_request_id() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint64_t> dist;
    std::stringstream ss;
    ss << "chatcmpl-" << std::hex << dist(gen);
    return ss.str();
}

static crow::response make_error(int status, const std::string& message,
                                  const std::string& type = "invalid_request_error") {
    json err = {
        {"error", {
            {"message", message},
            {"type", type},
            {"code", status}
        }}
    };
    crow::response resp(status);
    resp.set_header("Content-Type", "application/json");
    resp.body = err.dump();
    return resp;
}

void register_chat_completions(crow::SimpleApp& app) {

    // ── OpenAI-compatible chat completions ──────────────────────────
    CROW_ROUTE(app, "/v1/chat/completions").methods("POST"_method)
    ([](const crow::request& req) {
        auto& logger  = RequestLogger::instance();
        auto& metrics = Metrics::instance();
        metrics.inc_requests();

        std::string req_id = generate_request_id();

        // ── 1. Parse request body ────────────────────────────────────
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return make_error(400, "Invalid JSON body");
        }

        ChatRequest chat_req;
        chat_req.request_id = req_id;
        chat_req.model = body.value("model", "gpt-4o-mini");
        chat_req.temperature = body.value("temperature", 0.7);
        chat_req.max_tokens = body.value("max_tokens", 1024);
        chat_req.stream = body.value("stream", false);

        if (body.contains("user")) chat_req.user = body["user"].get<std::string>();

        if (!body.contains("messages") || !body["messages"].is_array() || body["messages"].empty()) {
            return make_error(400, "messages is required and must be a non-empty array");
        }

        for (auto& m : body["messages"]) {
            chat_req.messages.push_back({
                m.value("role", "user"),
                m.value("content", "")
            });
        }

        // ── 2. Auth ──────────────────────────────────────────────────
        std::string auth_header = std::string(req.get_header_value("Authorization"));
        if (!auth_header.empty()) {
            chat_req.api_key = auth_header;
            auto key_info = AuthMiddleware::instance().authenticate(auth_header);
            // Auth is optional for now — if key provided but invalid, warn
            if (!key_info && auth_header.find("Bearer ") != std::string::npos) {
                spdlog::debug("Unknown API key provided, proceeding without auth");
            }
        }

        logger.log_request(chat_req);

        // ── 3. Rate limiting ─────────────────────────────────────────
        if (!chat_req.api_key.empty()) {
            int est_tokens = TokenCounter::count_messages(chat_req.messages);
            auto rl = RateLimiter::instance().check_request(chat_req.api_key, est_tokens);
            if (!rl.allowed) {
                logger.log_rate_limited(req_id, chat_req.api_key);
                auto resp = make_error(429, "Rate limit exceeded");
                resp.set_header("Retry-After", std::to_string(rl.retry_after_sec));
                resp.set_header("X-RateLimit-Remaining-Requests", std::to_string(rl.remaining_rpm));
                resp.set_header("X-RateLimit-Remaining-Tokens", std::to_string(rl.remaining_tpm));
                return resp;
            }
        }

        // ── 4. Cache lookup ──────────────────────────────────────────
        auto cached = Cache::instance().get(chat_req);
        if (cached) {
            logger.log_cache_hit(req_id);
            metrics.inc_cache_hits();

            json result = {
                {"id", req_id},
                {"object", "chat.completion"},
                {"model", cached->model},
                {"choices", json::array({
                    {{"index", 0},
                     {"message", {{"role", "assistant"}, {"content", cached->content}}},
                     {"finish_reason", "stop"}}
                })},
                {"usage", {
                    {"prompt_tokens", cached->prompt_tokens},
                    {"completion_tokens", cached->completion_tokens},
                    {"total_tokens", cached->total_tokens}
                }},
                {"x_gateway", {
                    {"provider", cached->provider},
                    {"cache", "hit"},
                    {"latency_ms", 0},
                    {"cost_usd", 0}
                }}
            };

            crow::response resp(200);
            resp.set_header("Content-Type", "application/json");
            resp.set_header("X-Gateway-Cache", "HIT");
            resp.body = result.dump();
            return resp;
        }
        metrics.inc_cache_misses();

        // ── 5. Route to provider ─────────────────────────────────────
        auto decision = Router::instance().route(chat_req);
        if (decision.provider.empty()) {
            metrics.inc_errors();
            return make_error(503, "No available providers for model: " + chat_req.model);
        }

        auto provider = ProviderRegistry::instance().get(decision.provider);
        if (!provider) {
            metrics.inc_errors();
            return make_error(503, "Provider not available: " + decision.provider);
        }

        // ── 6. Call provider (with fallback) ─────────────────────────
        ChatResponse chat_resp;
        bool success = false;

        // Try primary
        ChatRequest provider_req = chat_req;
        provider_req.model = decision.model;

        try {
            chat_resp = provider->chat(provider_req);
            Router::instance().report_success(decision.provider, chat_resp.latency_ms);
            success = true;
        } catch (const std::exception& e) {
            spdlog::error("Provider {} failed: {}", decision.provider, e.what());
            Router::instance().report_failure(decision.provider, e.what());

            // Try fallbacks
            for (auto& fb_name : decision.fallback_chain) {
                auto fb = ProviderRegistry::instance().get(fb_name);
                if (!fb) continue;

                try {
                    // Use first model from fallback provider
                    auto models = fb->supported_models();
                    if (models.empty()) continue;
                    provider_req.model = models[0];

                    chat_resp = fb->chat(provider_req);
                    chat_resp.provider = fb_name;
                    Router::instance().report_success(fb_name, chat_resp.latency_ms);
                    success = true;
                    spdlog::info("Fallback to {} succeeded", fb_name);
                    break;
                } catch (const std::exception& e2) {
                    Router::instance().report_failure(fb_name, e2.what());
                    spdlog::error("Fallback {} also failed: {}", fb_name, e2.what());
                }
            }
        }

        if (!success) {
            metrics.inc_errors();
            logger.log_error(req_id, 502, "All providers failed");
            return make_error(502, "All providers failed for model: " + chat_req.model);
        }

        // ── 7. Track cost ────────────────────────────────────────────
        auto& cost_tracker = CostTracker::instance();
        double cost = cost_tracker.estimate_cost(
            chat_resp.provider, chat_resp.model,
            chat_resp.prompt_tokens, chat_resp.completion_tokens);
        chat_resp.cost_usd = cost;
        chat_resp.request_id = req_id;

        RequestMetrics rm;
        rm.request_id       = req_id;
        rm.api_key          = chat_req.api_key;
        rm.provider         = chat_resp.provider;
        rm.model            = chat_resp.model;
        rm.prompt_tokens    = chat_resp.prompt_tokens;
        rm.completion_tokens = chat_resp.completion_tokens;
        rm.latency_ms       = chat_resp.latency_ms;
        rm.cost_usd         = cost;
        cost_tracker.record(rm);

        // Record tokens for rate limiting
        if (!chat_req.api_key.empty()) {
            RateLimiter::instance().record_tokens(chat_req.api_key, chat_resp.total_tokens);
        }

        metrics.observe_latency(chat_resp.latency_ms);
        metrics.observe_tokens(chat_resp.total_tokens);

        // ── 8. Cache the response ────────────────────────────────────
        Cache::instance().put(chat_req, chat_resp);

        // ── 9. Return OpenAI-compatible response ─────────────────────
        logger.log_response(chat_resp);

        json result = {
            {"id", req_id},
            {"object", "chat.completion"},
            {"model", chat_resp.model},
            {"choices", json::array({
                {{"index", 0},
                 {"message", {{"role", "assistant"}, {"content", chat_resp.content}}},
                 {"finish_reason", "stop"}}
            })},
            {"usage", {
                {"prompt_tokens", chat_resp.prompt_tokens},
                {"completion_tokens", chat_resp.completion_tokens},
                {"total_tokens", chat_resp.total_tokens}
            }},
            {"x_gateway", {
                {"provider", chat_resp.provider},
                {"cache", "miss"},
                {"latency_ms", chat_resp.latency_ms},
                {"cost_usd", chat_resp.cost_usd},
                {"routing_strategy", strategy_to_string(decision.strategy_used)}
            }}
        };

        crow::response resp(200);
        resp.set_header("Content-Type", "application/json");
        resp.set_header("X-Gateway-Cache", "MISS");
        resp.set_header("X-Gateway-Provider", chat_resp.provider);
        resp.set_header("X-Gateway-Latency-Ms", std::to_string((int)chat_resp.latency_ms));
        resp.body = result.dump();
        return resp;
    });

    // ── Legacy /chat endpoint (backwards compatible with original) ───
    CROW_ROUTE(app, "/chat").methods("POST"_method)
    ([](const crow::request& req) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return make_error(400, "Invalid JSON");
        }

        // Convert old format to new
        std::string prompt = body.value("prompt", "");
        if (prompt.empty()) return make_error(400, "prompt is required");

        json new_body = {
            {"model", "gpt-4o-mini"},
            {"messages", json::array({{{"role", "user"}, {"content", prompt}}})}
        };

        // Forward internally
        ChatRequest chat_req;
        chat_req.request_id = generate_request_id();
        chat_req.model = "gpt-4o-mini";
        chat_req.messages = {{"user", prompt}};

        auto cached = Cache::instance().get(chat_req);
        if (cached) {
            json result;
            result["response"] = cached->content;
            result["cached"] = true;
            crow::response resp(200);
            resp.set_header("Content-Type", "application/json");
            resp.body = result.dump();
            return resp;
        }

        auto decision = Router::instance().route(chat_req);
        auto provider = ProviderRegistry::instance().get(decision.provider);
        if (!provider) return make_error(503, "No providers available");

        try {
            ChatRequest pr = chat_req;
            pr.model = decision.model;
            auto chat_resp = provider->chat(pr);
            Cache::instance().put(chat_req, chat_resp);

            json result;
            result["response"] = chat_resp.content;
            result["provider"] = chat_resp.provider;
            result["model"] = chat_resp.model;
            result["cached"] = false;
            crow::response resp(200);
            resp.set_header("Content-Type", "application/json");
            resp.body = result.dump();
            return resp;
        } catch (const std::exception& e) {
            return make_error(502, e.what());
        }
    });
}

}} // namespace gateway::api

// ═══════════════════════════════════════════════════════════════════════
// LLM Gateway — High-Performance Multi-Provider LLM Routing
// Cost-optimized C++ gateway with caching, auth, and smart routing
// ═══════════════════════════════════════════════════════════════════════

#include "config.h"
#include "router.h"
#include "cache.h"
#include "cost_tracker.h"
#include "providers/provider_registry.h"
#include "middleware/auth.h"
#include "api/endpoints.h"
#include "utils/metrics.h"

#include <crow.h>
#include <spdlog/spdlog.h>
#include <csignal>
#include <iostream>

static crow::SimpleApp* g_app = nullptr;

void signal_handler(int sig) {
    spdlog::info("Received signal {}, shutting down...", sig);
    if (g_app) g_app->stop();
}

int main(int argc, char* argv[]) {
    // ── Banner ──────────────────────────────────────────────────────
    std::cout << R"(
  ╔═══════════════════════════════════════════╗
  ║  LLM Gateway v1.0.0                      ║
  ║  High-performance multi-provider routing  ║
  ╚═══════════════════════════════════════════╝
)" << std::endl;

    // ── Load config ─────────────────────────────────────────────────
    std::string config_path = "config/gateway.json";
    if (argc > 1) config_path = argv[1];

    auto& config = gateway::Config::instance();
    config.load_from_file(config_path);
    config.load_from_env();

    auto& cfg = config.get();

    // ── Setup logging ───────────────────────────────────────────────
    if (cfg.server.log_level == "debug")      spdlog::set_level(spdlog::level::debug);
    else if (cfg.server.log_level == "warn")  spdlog::set_level(spdlog::level::warn);
    else if (cfg.server.log_level == "error") spdlog::set_level(spdlog::level::err);
    else                                      spdlog::set_level(spdlog::level::info);

    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");

    // ── Initialize providers ────────────────────────────────────────
    auto& registry = gateway::ProviderRegistry::instance();
    registry.initialize(cfg.providers);

    auto providers = registry.available_providers();
    if (providers.empty()) {
        spdlog::warn("No providers configured! Set API keys via env vars or config file.");
        spdlog::warn("  OPENAI_API_KEY, ANTHROPIC_API_KEY, TOGETHER_API_KEY, GROQ_API_KEY");
    } else {
        spdlog::info("Active providers: {}", [&]() {
            std::string s;
            for (auto& p : providers) { if (!s.empty()) s += ", "; s += p; }
            return s;
        }());
    }

    // ── Initialize subsystems ───────────────────────────────────────
    auto& router  = gateway::Router::instance();
    auto& cache   = gateway::Cache::instance();
    auto& costs   = gateway::CostTracker::instance();
    auto& metrics = gateway::Metrics::instance();

    spdlog::info("Routing strategy: {}", gateway::strategy_to_string(router.get_strategy()));
    spdlog::info("Cache: {} (max={}, ttl={}s)",
                 cfg.cache.enabled ? "enabled" : "disabled",
                 cfg.cache.max_entries, cfg.cache.ttl_seconds);

    // ── Setup Crow app ──────────────────────────────────────────────
    crow::SimpleApp app;
    g_app = &app;

    // CORS middleware
    if (cfg.server.enable_cors) {
        auto& cors = app.get_middleware<crow::CORSHandler>();
        // Note: if Crow version doesn't have built-in CORS,
        // we handle it manually in a catchall
    }

    // Register all API routes
    gateway::api::register_chat_completions(app);
    gateway::api::register_health(app);
    gateway::api::register_admin(app);

    // ── Root endpoint ───────────────────────────────────────────────
    CROW_ROUTE(app, "/")
    ([]() {
        gateway::json result = {
            {"name", "LLM Gateway"},
            {"version", "1.0.0"},
            {"endpoints", {
                "/v1/chat/completions",
                "/v1/models",
                "/v1/stats",
                "/chat",
                "/health",
                "/metrics",
                "/admin/keys",
                "/admin/routing",
                "/admin/cache",
                "/admin/costs"
            }}
        };
        crow::response resp(200);
        resp.set_header("Content-Type", "application/json");
        resp.body = result.dump(2);
        return resp;
    });

    // ── Signal handling ─────────────────────────────────────────────
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    // ── Start server ────────────────────────────────────────────────
    spdlog::info("Starting LLM Gateway on {}:{} ({} threads)",
                 cfg.server.host, cfg.server.port, cfg.server.threads);

    app.bindaddr(cfg.server.host)
       .port(cfg.server.port)
       .concurrency(cfg.server.threads)
       .run();

    spdlog::info("LLM Gateway stopped");
    return 0;
}

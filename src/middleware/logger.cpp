#include "middleware/logger.h"
#include <spdlog/spdlog.h>

namespace gateway {

RequestLogger& RequestLogger::instance() {
    static RequestLogger inst;
    return inst;
}

void RequestLogger::log_request(const ChatRequest& req) {
    spdlog::info("[REQ] id={} model={} messages={} stream={} key={}",
                 req.request_id,
                 req.model,
                 req.messages.size(),
                 req.stream,
                 req.api_key.empty() ? "none" : req.api_key.substr(0, 8) + "...");
}

void RequestLogger::log_response(const ChatResponse& resp) {
    spdlog::info("[RES] id={} provider={} model={} tokens={} latency={:.1f}ms cost=${:.6f} cache={}",
                 resp.request_id,
                 resp.provider,
                 resp.model,
                 resp.total_tokens,
                 resp.latency_ms,
                 resp.cost_usd,
                 resp.from_cache ? "HIT" : "MISS");
}

void RequestLogger::log_error(const std::string& request_id, int status, const std::string& msg) {
    spdlog::error("[ERR] id={} status={} error={}", request_id, status, msg);
}

void RequestLogger::log_cache_hit(const std::string& request_id) {
    spdlog::info("[CACHE] id={} HIT", request_id);
}

void RequestLogger::log_rate_limited(const std::string& request_id, const std::string& api_key) {
    spdlog::warn("[RATE] id={} key={} THROTTLED", request_id,
                 api_key.empty() ? "none" : api_key.substr(0, 8) + "...");
}

} // namespace gateway

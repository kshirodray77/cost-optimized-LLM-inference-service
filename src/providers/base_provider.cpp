#include "providers/base_provider.h"
#include <httplib.h>
#include <spdlog/spdlog.h>
#include <thread>

namespace gateway {

bool BaseProvider::supports_model(const std::string& model) const {
    auto models = supported_models();
    return std::find(models.begin(), models.end(), model) != models.end();
}

void BaseProvider::chat_stream(const ChatRequest& req, StreamCallback cb) {
    // Default: non-streaming fallback
    auto resp = chat(req);
    cb({resp.content, true, resp.prompt_tokens, resp.completion_tokens});
}

bool BaseProvider::health_check() {
    try {
        ChatRequest req;
        req.request_id = "health-check";
        req.model = supported_models().empty() ? "" : supported_models()[0];
        req.messages = {{"user", "hi"}};
        req.max_tokens = 1;
        req.temperature = 0;
        auto resp = chat(req);
        return !resp.content.empty();
    } catch (...) {
        return false;
    }
}

json BaseProvider::http_post(const std::string& path, const json& body,
                              const std::unordered_map<std::string, std::string>& headers) {
    // Parse base_url into host + optional port
    std::string url = base_url_;

    // Use cpp-httplib for HTTPS
    httplib::Client cli(url);
    cli.set_connection_timeout(timeout_ms_ / 1000, (timeout_ms_ % 1000) * 1000);
    cli.set_read_timeout(timeout_ms_ / 1000, (timeout_ms_ % 1000) * 1000);

    httplib::Headers hdrs;
    for (auto& [k, v] : headers) hdrs.emplace(k, v);
    hdrs.emplace("Content-Type", "application/json");

    std::string body_str = body.dump();
    auto result = cli.Post(path, hdrs, body_str, "application/json");

    if (!result) {
        throw std::runtime_error("HTTP request failed: " + httplib::to_string(result.error()));
    }

    if (result->status >= 400) {
        throw std::runtime_error("HTTP " + std::to_string(result->status) + ": " + result->body);
    }

    return json::parse(result->body);
}

ChatResponse BaseProvider::with_retry(std::function<ChatResponse()> fn) {
    std::exception_ptr last_ex;
    for (int attempt = 0; attempt <= max_retries_; ++attempt) {
        try {
            return fn();
        } catch (const std::exception& e) {
            last_ex = std::current_exception();
            spdlog::warn("{} attempt {}/{} failed: {}", name(), attempt + 1, max_retries_ + 1, e.what());
            if (attempt < max_retries_) {
                int delay_ms = 100 * (1 << attempt);  // Exponential backoff
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }
        }
    }
    std::rethrow_exception(last_ex);
}

} // namespace gateway

#include "providers/anthropic_provider.h"
#include <spdlog/spdlog.h>

namespace gateway {

AnthropicProvider::AnthropicProvider() {
    base_url_ = "https://api.anthropic.com";
}

std::vector<std::string> AnthropicProvider::supported_models() const {
    return {
        "claude-sonnet-4-20250514",
        "claude-haiku-3-5",
        "claude-opus-4"
    };
}

json AnthropicProvider::build_request_body(const ChatRequest& req) const {
    // Anthropic uses a different format: separate system from messages
    json messages = json::array();
    std::string system_prompt;

    for (auto& msg : req.messages) {
        if (msg.role == "system") {
            system_prompt += msg.content + "\n";
        } else {
            messages.push_back({{"role", msg.role}, {"content", msg.content}});
        }
    }

    json body = {
        {"model", req.model},
        {"messages", messages},
        {"max_tokens", req.max_tokens},
        {"temperature", req.temperature}
    };

    if (!system_prompt.empty()) {
        body["system"] = system_prompt;
    }

    if (req.stream) body["stream"] = true;

    return body;
}

ChatResponse AnthropicProvider::chat(const ChatRequest& req) {
    return with_retry([&]() {
        auto start = Clock::now();

        json resp = http_post("/v1/messages", build_request_body(req), {
            {"x-api-key", api_key_},
            {"anthropic-version", "2023-06-01"}
        });

        auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        return parse_response(resp, req, elapsed);
    });
}

ChatResponse AnthropicProvider::parse_response(const json& resp, const ChatRequest& req,
                                                double latency_ms) const {
    ChatResponse r;
    r.request_id = req.request_id;
    r.provider   = "anthropic";
    r.model      = resp.value("model", req.model);
    r.latency_ms = latency_ms;

    // Anthropic returns content as array of blocks
    if (resp.contains("content") && resp["content"].is_array()) {
        for (auto& block : resp["content"]) {
            if (block.value("type", "") == "text") {
                r.content += block.value("text", "");
            }
        }
    }

    if (resp.contains("usage")) {
        r.prompt_tokens     = resp["usage"].value("input_tokens", 0);
        r.completion_tokens = resp["usage"].value("output_tokens", 0);
        r.total_tokens      = r.prompt_tokens + r.completion_tokens;
    }

    r.raw_response = resp;
    return r;
}

void AnthropicProvider::chat_stream(const ChatRequest& req, StreamCallback cb) {
    auto resp = chat(req);
    cb({resp.content, true, resp.prompt_tokens, resp.completion_tokens});
}

} // namespace gateway

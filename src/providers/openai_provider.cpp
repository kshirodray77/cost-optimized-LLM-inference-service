#include "providers/openai_provider.h"
#include <spdlog/spdlog.h>

namespace gateway {

OpenAIProvider::OpenAIProvider() {
    base_url_ = "https://api.openai.com";
}

std::vector<std::string> OpenAIProvider::supported_models() const {
    return {
        "gpt-4o", "gpt-4o-mini", "gpt-4-turbo",
        "o1", "o1-mini", "o3-mini",
        "gpt-3.5-turbo"
    };
}

json OpenAIProvider::build_request_body(const ChatRequest& req) const {
    json messages = json::array();
    for (auto& msg : req.messages) {
        messages.push_back({{"role", msg.role}, {"content", msg.content}});
    }

    json body = {
        {"model", req.model},
        {"messages", messages},
        {"temperature", req.temperature},
        {"max_tokens", req.max_tokens}
    };

    if (req.stream) body["stream"] = true;
    if (req.user) body["user"] = *req.user;

    // Merge any extra params
    if (!req.extra.empty()) body.merge_patch(req.extra);

    return body;
}

ChatResponse OpenAIProvider::chat(const ChatRequest& req) {
    return with_retry([&]() {
        auto start = Clock::now();

        json resp = http_post("/v1/chat/completions", build_request_body(req), {
            {"Authorization", "Bearer " + api_key_}
        });

        auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        return parse_response(resp, req, elapsed);
    });
}

ChatResponse OpenAIProvider::parse_response(const json& resp, const ChatRequest& req,
                                             double latency_ms) const {
    ChatResponse r;
    r.request_id = req.request_id;
    r.provider   = "openai";
    r.latency_ms = latency_ms;

    r.model = resp.value("model", req.model);

    if (resp.contains("choices") && !resp["choices"].empty()) {
        auto& choice = resp["choices"][0];
        if (choice.contains("message")) {
            r.content = choice["message"].value("content", "");
        }
    }

    if (resp.contains("usage")) {
        r.prompt_tokens     = resp["usage"].value("prompt_tokens", 0);
        r.completion_tokens = resp["usage"].value("completion_tokens", 0);
        r.total_tokens      = resp["usage"].value("total_tokens", 0);
    }

    r.raw_response = resp;
    return r;
}

void OpenAIProvider::chat_stream(const ChatRequest& req, StreamCallback cb) {
    // For streaming, we'd use chunked transfer encoding
    // Simplified: fall back to non-streaming and emit as single chunk
    auto resp = chat(req);
    cb({resp.content, true, resp.prompt_tokens, resp.completion_tokens});
}

} // namespace gateway

#include "providers/groq_provider.h"
#include <spdlog/spdlog.h>

namespace gateway {

GroqProvider::GroqProvider() {
    base_url_ = "https://api.groq.com";
}

std::vector<std::string> GroqProvider::supported_models() const {
    return {
        "llama-3.1-8b-instant",
        "llama-3.1-70b-versatile",
        "llama-3.3-70b-versatile",
        "mixtral-8x7b-32768",
        "gemma2-9b-it"
    };
}

json GroqProvider::build_request_body(const ChatRequest& req) const {
    // Groq uses OpenAI-compatible format
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
    return body;
}

ChatResponse GroqProvider::chat(const ChatRequest& req) {
    return with_retry([&]() {
        auto start = Clock::now();

        json resp = http_post("/openai/v1/chat/completions", build_request_body(req), {
            {"Authorization", "Bearer " + api_key_}
        });

        auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        return parse_response(resp, req, elapsed);
    });
}

ChatResponse GroqProvider::parse_response(const json& resp, const ChatRequest& req,
                                           double latency_ms) const {
    ChatResponse r;
    r.request_id = req.request_id;
    r.provider   = "groq";
    r.model      = resp.value("model", req.model);
    r.latency_ms = latency_ms;

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

void GroqProvider::chat_stream(const ChatRequest& req, StreamCallback cb) {
    auto resp = chat(req);
    cb({resp.content, true, resp.prompt_tokens, resp.completion_tokens});
}

} // namespace gateway

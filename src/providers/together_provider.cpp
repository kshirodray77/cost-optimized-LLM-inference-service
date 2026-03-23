#include "providers/together_provider.h"
#include <spdlog/spdlog.h>

namespace gateway {

TogetherProvider::TogetherProvider() {
    base_url_ = "https://api.together.xyz";
}

std::vector<std::string> TogetherProvider::supported_models() const {
    return {
        "meta-llama/Llama-3.1-8B-Instruct",
        "meta-llama/Llama-3.1-70B-Instruct",
        "meta-llama/Llama-3.3-70B-Instruct",
        "mistralai/Mixtral-8x7B-Instruct-v0.1",
        "Qwen/Qwen2.5-72B-Instruct"
    };
}

json TogetherProvider::build_request_body(const ChatRequest& req) const {
    // Together uses OpenAI-compatible format
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

ChatResponse TogetherProvider::chat(const ChatRequest& req) {
    return with_retry([&]() {
        auto start = Clock::now();

        json resp = http_post("/v1/chat/completions", build_request_body(req), {
            {"Authorization", "Bearer " + api_key_}
        });

        auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        return parse_response(resp, req, elapsed);
    });
}

ChatResponse TogetherProvider::parse_response(const json& resp, const ChatRequest& req,
                                               double latency_ms) const {
    ChatResponse r;
    r.request_id = req.request_id;
    r.provider   = "together";
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

void TogetherProvider::chat_stream(const ChatRequest& req, StreamCallback cb) {
    auto resp = chat(req);
    cb({resp.content, true, resp.prompt_tokens, resp.completion_tokens});
}

} // namespace gateway

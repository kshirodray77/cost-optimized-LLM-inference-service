#pragma once
#include "providers/base_provider.h"

namespace gateway {

class AnthropicProvider : public BaseProvider {
public:
    AnthropicProvider();
    std::string name() const override { return "anthropic"; }
    std::vector<std::string> supported_models() const override;
    ChatResponse chat(const ChatRequest& req) override;
    bool supports_streaming() const override { return true; }
    void chat_stream(const ChatRequest& req, StreamCallback cb) override;

private:
    json build_request_body(const ChatRequest& req) const;
    ChatResponse parse_response(const json& resp, const ChatRequest& req,
                                double latency_ms) const;
};

} // namespace gateway

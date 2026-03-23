#pragma once
// ═══════════════════════════════════════════════════════════════════════
// Base Provider — Abstract interface for all LLM providers
// ═══════════════════════════════════════════════════════════════════════

#include "types.h"
#include <memory>

namespace gateway {

class BaseProvider {
public:
    virtual ~BaseProvider() = default;

    // Provider identity
    virtual std::string name() const = 0;
    virtual std::vector<std::string> supported_models() const = 0;
    virtual bool supports_model(const std::string& model) const;

    // Chat completion
    virtual ChatResponse chat(const ChatRequest& req) = 0;

    // Streaming (default: not supported, falls back to non-stream)
    virtual bool supports_streaming() const { return false; }
    virtual void chat_stream(const ChatRequest& req, StreamCallback cb);

    // Health check
    virtual bool health_check();

    // Configuration
    void set_api_key(const std::string& key) { api_key_ = key; }
    void set_base_url(const std::string& url) { base_url_ = url; }
    void set_timeout(int ms) { timeout_ms_ = ms; }
    void set_max_retries(int n) { max_retries_ = n; }

protected:
    std::string api_key_;
    std::string base_url_;
    int timeout_ms_   = 30000;
    int max_retries_  = 2;

    // Shared HTTP helpers
    json http_post(const std::string& path, const json& body,
                   const std::unordered_map<std::string, std::string>& headers = {});

    // Retry wrapper
    ChatResponse with_retry(std::function<ChatResponse()> fn);
};

using ProviderPtr = std::shared_ptr<BaseProvider>;

} // namespace gateway

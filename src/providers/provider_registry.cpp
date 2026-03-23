#include "providers/provider_registry.h"
#include "providers/openai_provider.h"
#include "providers/anthropic_provider.h"
#include "providers/together_provider.h"
#include "providers/groq_provider.h"
#include <spdlog/spdlog.h>

namespace gateway {

ProviderRegistry& ProviderRegistry::instance() {
    static ProviderRegistry inst;
    return inst;
}

void ProviderRegistry::initialize(const std::vector<ProviderConfig>& configs) {
    for (auto& cfg : configs) {
        if (!cfg.enabled) {
            spdlog::info("Provider {} is disabled, skipping", cfg.name);
            continue;
        }
        if (cfg.api_key.empty()) {
            spdlog::warn("Provider {} has no API key, skipping", cfg.name);
            continue;
        }

        auto provider = create_provider(cfg);
        if (provider) {
            providers_[cfg.name] = provider;
            spdlog::info("Provider {} initialized with {} models",
                         cfg.name, provider->supported_models().size());
        }
    }

    spdlog::info("ProviderRegistry: {} providers active", providers_.size());
}

ProviderPtr ProviderRegistry::get(const std::string& name) const {
    auto it = providers_.find(name);
    return (it != providers_.end()) ? it->second : nullptr;
}

std::vector<std::string> ProviderRegistry::available_providers() const {
    std::vector<std::string> names;
    for (auto& [k, _] : providers_) names.push_back(k);
    return names;
}

bool ProviderRegistry::has_provider(const std::string& name) const {
    return providers_.count(name) > 0;
}

ProviderPtr ProviderRegistry::create_provider(const ProviderConfig& cfg) {
    ProviderPtr p;

    if (cfg.name == "openai")         p = std::make_shared<OpenAIProvider>();
    else if (cfg.name == "anthropic") p = std::make_shared<AnthropicProvider>();
    else if (cfg.name == "together")  p = std::make_shared<TogetherProvider>();
    else if (cfg.name == "groq")      p = std::make_shared<GroqProvider>();
    else {
        spdlog::error("Unknown provider: {}", cfg.name);
        return nullptr;
    }

    p->set_api_key(cfg.api_key);
    if (!cfg.base_url.empty()) p->set_base_url(cfg.base_url);
    p->set_timeout(cfg.timeout_ms);
    p->set_max_retries(cfg.max_retries);

    return p;
}

} // namespace gateway

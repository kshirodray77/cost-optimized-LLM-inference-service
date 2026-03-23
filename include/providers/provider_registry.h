#pragma once
#include "providers/base_provider.h"
#include "config.h"
#include <unordered_map>

namespace gateway {

class ProviderRegistry {
public:
    static ProviderRegistry& instance();

    void initialize(const std::vector<ProviderConfig>& configs);
    ProviderPtr get(const std::string& name) const;
    std::vector<std::string> available_providers() const;
    bool has_provider(const std::string& name) const;

private:
    ProviderRegistry() = default;
    std::unordered_map<std::string, ProviderPtr> providers_;

    ProviderPtr create_provider(const ProviderConfig& config);
};

} // namespace gateway

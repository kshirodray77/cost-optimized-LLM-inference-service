#pragma once
#include "types.h"
#include <mutex>

namespace gateway {

class AuthMiddleware {
public:
    static AuthMiddleware& instance();

    // Validate API key, return the key info if valid
    std::optional<VirtualKey> authenticate(const std::string& bearer_token);

    // Key management
    VirtualKey create_key(const std::string& owner, const std::string& tier = "free");
    bool revoke_key(const std::string& key_id);
    std::vector<VirtualKey> list_keys() const;
    std::optional<VirtualKey> get_key(const std::string& key_id) const;

    // Admin check
    bool is_admin(const std::string& token) const;

private:
    AuthMiddleware() = default;
    mutable std::mutex mu_;
    std::unordered_map<std::string, VirtualKey> keys_;  // key_hash -> VirtualKey

    std::string hash_key(const std::string& raw_key) const;
    std::string generate_key() const;
};

} // namespace gateway

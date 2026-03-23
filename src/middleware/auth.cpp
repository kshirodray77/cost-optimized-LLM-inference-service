#include "middleware/auth.h"
#include "config.h"
#include <spdlog/spdlog.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <functional>

namespace gateway {

AuthMiddleware& AuthMiddleware::instance() {
    static AuthMiddleware inst;
    return inst;
}

std::optional<VirtualKey> AuthMiddleware::authenticate(const std::string& bearer_token) {
    if (bearer_token.empty()) return std::nullopt;

    // Strip "Bearer " prefix
    std::string token = bearer_token;
    if (token.substr(0, 7) == "Bearer ") {
        token = token.substr(7);
    }

    std::lock_guard<std::mutex> lock(mu_);
    std::string h = hash_key(token);
    auto it = keys_.find(h);
    if (it == keys_.end()) {
        spdlog::debug("Auth: unknown key hash={}", h.substr(0, 8));
        return std::nullopt;
    }

    auto& key = it->second;
    if (!key.active) {
        spdlog::debug("Auth: key {} is inactive", key.key_id);
        return std::nullopt;
    }

    // Check expiry
    if (key.expires_at != TimePoint{} && Clock::now() > key.expires_at) {
        spdlog::debug("Auth: key {} is expired", key.key_id);
        return std::nullopt;
    }

    return key;
}

VirtualKey AuthMiddleware::create_key(const std::string& owner, const std::string& tier) {
    std::lock_guard<std::mutex> lock(mu_);

    std::string raw = generate_key();
    std::string h   = hash_key(raw);

    VirtualKey vk;
    vk.key_id   = "gw-" + raw.substr(0, 8);
    vk.key_hash = h;
    vk.owner    = owner;
    vk.tier     = tier;

    // Set limits by tier
    if (tier == "free") {
        vk.rate_limit_rpm = 20;
        vk.rate_limit_tpm = 40000;
        vk.budget_usd     = 5.0;
    } else if (tier == "pro") {
        vk.rate_limit_rpm = 120;
        vk.rate_limit_tpm = 500000;
        vk.budget_usd     = 100.0;
    } else if (tier == "team") {
        vk.rate_limit_rpm = 600;
        vk.rate_limit_tpm = 2000000;
        vk.budget_usd     = 1000.0;
    } else if (tier == "enterprise") {
        vk.rate_limit_rpm = 6000;
        vk.rate_limit_tpm = 20000000;
        vk.budget_usd     = 0;  // Unlimited
    }

    keys_[h] = vk;
    spdlog::info("Created API key {} for {} (tier={})", vk.key_id, owner, tier);

    // Return with the raw key (only time it's visible)
    VirtualKey result = vk;
    result.key_hash = raw;  // Return raw key, not hash, so user can save it
    return result;
}

bool AuthMiddleware::revoke_key(const std::string& key_id) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& [h, vk] : keys_) {
        if (vk.key_id == key_id) {
            vk.active = false;
            spdlog::info("Revoked API key {}", key_id);
            return true;
        }
    }
    return false;
}

std::vector<VirtualKey> AuthMiddleware::list_keys() const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<VirtualKey> result;
    for (auto& [_, vk] : keys_) {
        VirtualKey safe = vk;
        safe.key_hash = "***";  // Don't expose hash
        result.push_back(safe);
    }
    return result;
}

std::optional<VirtualKey> AuthMiddleware::get_key(const std::string& key_id) const {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& [_, vk] : keys_) {
        if (vk.key_id == key_id) return vk;
    }
    return std::nullopt;
}

bool AuthMiddleware::is_admin(const std::string& token) const {
    auto& admin_key = Config::instance().get().server.admin_key;
    return !admin_key.empty() && token == admin_key;
}

std::string AuthMiddleware::hash_key(const std::string& raw_key) const {
    // Simple hash for demo — in production use SHA-256
    size_t h = std::hash<std::string>{}(raw_key);
    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << h;
    return ss.str();
}

std::string AuthMiddleware::generate_key() const {
    static const char charset[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, sizeof(charset) - 2);

    std::string key = "sk-gw-";
    for (int i = 0; i < 32; ++i) {
        key += charset[dist(gen)];
    }
    return key;
}

} // namespace gateway

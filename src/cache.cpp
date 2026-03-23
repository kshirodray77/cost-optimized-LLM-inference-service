#include "cache.h"
#include "config.h"
#include "utils/token_counter.h"
#include <spdlog/spdlog.h>
#include <functional>

namespace gateway {

Cache& Cache::instance() {
    static Cache inst;
    return inst;
}

Cache::Cache() {
    auto& cfg = Config::instance().get().cache;
    max_size_    = cfg.max_entries;
    ttl_seconds_ = cfg.ttl_seconds;
}

std::string Cache::make_key(const ChatRequest& req) const {
    // Build a deterministic cache key from the request
    std::string key = req.model + "|";
    for (auto& msg : req.messages) {
        key += msg.role + ":" + msg.content + "|";
    }
    if (Config::instance().get().cache.match_params) {
        key += "t=" + std::to_string(req.temperature) + "|";
        key += "m=" + std::to_string(req.max_tokens);
    }

    // Simple hash to keep keys short
    size_t h = std::hash<std::string>{}(key);
    return std::to_string(h);
}

std::optional<ChatResponse> Cache::get(const ChatRequest& req) {
    if (!Config::instance().get().cache.enabled) {
        misses_++;
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(mu_);
    std::string key = make_key(req);

    auto it = index_.find(key);
    if (it == index_.end()) {
        misses_++;
        return std::nullopt;
    }

    auto& entry = *(it->second);

    // Check TTL
    if (Clock::now() > entry.expires_at) {
        entries_.erase(it->second);
        index_.erase(it);
        misses_++;
        return std::nullopt;
    }

    // Move to front (most recently used)
    entries_.splice(entries_.begin(), entries_, it->second);
    entry.last_accessed = Clock::now();

    hits_++;
    savings_usd_ += entry.response.cost_usd;

    // Mark as cache hit
    ChatResponse resp = entry.response;
    resp.from_cache = true;
    resp.request_id = req.request_id;
    resp.latency_ms = 0;

    spdlog::debug("Cache HIT for key={}", key.substr(0, 16));
    return resp;
}

void Cache::put(const ChatRequest& req, const ChatResponse& resp) {
    if (!Config::instance().get().cache.enabled) return;
    if (req.stream) return;  // Don't cache streaming responses

    std::lock_guard<std::mutex> lock(mu_);

    // Re-read config in case it changed (e.g. in tests)
    max_size_    = Config::instance().get().cache.max_entries;
    ttl_seconds_ = Config::instance().get().cache.ttl_seconds;

    std::string key = make_key(req);

    // If already exists, update it
    auto it = index_.find(key);
    if (it != index_.end()) {
        entries_.erase(it->second);
        index_.erase(it);
    }

    // Evict if at capacity
    while (entries_.size() >= max_size_) {
        evict_lru();
    }

    // Also evict expired entries periodically
    if (entries_.size() > max_size_ / 2) {
        evict_expired();
    }

    // Insert at front
    auto now = Clock::now();
    CacheEntry entry{
        key, resp,
        now + std::chrono::seconds(ttl_seconds_),
        now
    };
    entries_.push_front(std::move(entry));
    index_[key] = entries_.begin();

    spdlog::debug("Cache PUT key={} (size={})", key.substr(0, 16), entries_.size());
}

void Cache::clear() {
    std::lock_guard<std::mutex> lock(mu_);
    entries_.clear();
    index_.clear();
    spdlog::info("Cache cleared");
}

size_t Cache::size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return entries_.size();
}

double Cache::hit_rate() const {
    uint64_t total = hits_ + misses_;
    return total > 0 ? static_cast<double>(hits_) / total : 0;
}

Cache::Stats Cache::get_stats() const {
    std::lock_guard<std::mutex> lock(mu_);
    return {entries_.size(), max_size_, hits_, misses_, evictions_, savings_usd_};
}

void Cache::evict_expired() {
    auto now = Clock::now();
    auto it = entries_.end();
    while (it != entries_.begin()) {
        --it;
        if (now > it->expires_at) {
            index_.erase(it->key);
            it = entries_.erase(it);
            evictions_++;
        }
    }
}

void Cache::evict_lru() {
    if (entries_.empty()) return;
    auto& back = entries_.back();
    index_.erase(back.key);
    entries_.pop_back();
    evictions_++;
}

} // namespace gateway

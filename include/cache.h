#pragma once
// ═══════════════════════════════════════════════════════════════════════
// Cache — Thread-safe LRU cache with TTL
// Replaces the original unbounded unordered_map
// ═══════════════════════════════════════════════════════════════════════

#include "types.h"
#include <mutex>
#include <list>

namespace gateway {

class Cache {
public:
    static Cache& instance();

    // Lookup: returns cached response if hit, nullopt if miss
    std::optional<ChatResponse> get(const ChatRequest& req);

    // Store a response
    void put(const ChatRequest& req, const ChatResponse& resp);

    // Cache management
    void clear();
    size_t size() const;
    double hit_rate() const;

    struct Stats {
        size_t entries    = 0;
        size_t max_size   = 0;
        uint64_t hits     = 0;
        uint64_t misses   = 0;
        uint64_t evictions = 0;
        double   savings_usd = 0;  // Estimated $ saved by cache hits
    };
    Stats get_stats() const;

private:
    Cache();

    struct CacheEntry {
        std::string key;
        ChatResponse response;
        TimePoint expires_at;
        TimePoint last_accessed;
    };

    mutable std::mutex mu_;
    std::list<CacheEntry> entries_;  // Front = most recent
    std::unordered_map<std::string, std::list<CacheEntry>::iterator> index_;
    size_t max_size_ = 10000;
    int ttl_seconds_ = 3600;

    // Stats
    uint64_t hits_     = 0;
    uint64_t misses_   = 0;
    uint64_t evictions_ = 0;
    double   savings_usd_ = 0;

    std::string make_key(const ChatRequest& req) const;
    void evict_expired();
    void evict_lru();
};

} // namespace gateway

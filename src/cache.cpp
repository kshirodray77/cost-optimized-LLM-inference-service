#include "cache.h"
#include <unordered_map>

static std::unordered_map<std::string, std::string> cache;

std::string get_cached(const std::string& prompt) {

    if (cache.find(prompt) != cache.end())
        return cache[prompt];

    return "";
}

void set_cache(const std::string& prompt, const std::string& response) {

    cache[prompt] = response;
}
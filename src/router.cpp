#include "router.h"
#include "cache.h"

std::string call_openai(const std::string& prompt);
std::string call_together(const std::string& prompt);

std::string route_request(const std::string& prompt) {

    std::string cached = get_cached(prompt);

    if (!cached.empty()) {
        return cached;
    }

    std::string response;

    if (prompt.size() < 200) {
        response = call_together(prompt);
    } else {
        response = call_openai(prompt);
    }

    set_cache(prompt, response);

    return response;
}
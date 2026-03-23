#pragma once
#include "types.h"

namespace gateway {

class RequestLogger {
public:
    static RequestLogger& instance();

    void log_request(const ChatRequest& req);
    void log_response(const ChatResponse& resp);
    void log_error(const std::string& request_id, int status, const std::string& msg);
    void log_cache_hit(const std::string& request_id);
    void log_rate_limited(const std::string& request_id, const std::string& api_key);

private:
    RequestLogger() = default;
};

} // namespace gateway

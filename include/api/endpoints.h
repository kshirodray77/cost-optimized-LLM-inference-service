#pragma once
#include <crow.h>

namespace gateway { namespace api {

void register_chat_completions(crow::SimpleApp& app);
void register_health(crow::SimpleApp& app);
void register_admin(crow::SimpleApp& app);

}} // namespace gateway::api

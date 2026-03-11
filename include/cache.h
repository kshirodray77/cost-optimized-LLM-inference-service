#pragma once
#include <string>

std::string get_cached(const std::string& prompt);
void set_cache(const std::string& prompt, const std::string& response);
#pragma once
#include <string>
#include <vector>
#include "types.h"

namespace gateway {

class TokenCounter {
public:
    // Approximate token count (fast, no tokenizer needed)
    static int count(const std::string& text);

    // Count tokens in a message array
    static int count_messages(const std::vector<Message>& messages);

    // More accurate: split into pseudo-tokens
    static std::vector<std::string> tokenize(const std::string& text);
};

} // namespace gateway

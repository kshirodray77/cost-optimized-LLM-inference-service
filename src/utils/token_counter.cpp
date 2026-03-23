#include "utils/token_counter.h"
#include <regex>
#include <cctype>

namespace gateway {

int TokenCounter::count(const std::string& text) {
    // Approximation: ~4 chars per token for English
    // More accurate than the original text.size()/4 because we handle
    // whitespace and punctuation better
    if (text.empty()) return 0;

    auto tokens = tokenize(text);
    return static_cast<int>(tokens.size());
}

int TokenCounter::count_messages(const std::vector<Message>& messages) {
    int total = 0;
    for (auto& msg : messages) {
        total += 4;  // Every message has overhead (~4 tokens for role/formatting)
        total += count(msg.content);
    }
    total += 2;  // Priming tokens
    return total;
}

std::vector<std::string> TokenCounter::tokenize(const std::string& text) {
    // Simple word-piece-like approximation
    // Not BPE-accurate, but good enough for cost estimation
    std::vector<std::string> tokens;
    std::string current;

    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];

        if (std::isspace(c)) {
            if (!current.empty()) {
                // Split long words into ~4-char chunks (BPE approximation)
                while (current.size() > 4) {
                    tokens.push_back(current.substr(0, 4));
                    current = current.substr(4);
                }
                if (!current.empty()) tokens.push_back(current);
                current.clear();
            }
            // Whitespace sometimes becomes its own token
            if (c == '\n') tokens.push_back("\n");
        } else if (std::ispunct(c)) {
            if (!current.empty()) {
                while (current.size() > 4) {
                    tokens.push_back(current.substr(0, 4));
                    current = current.substr(4);
                }
                if (!current.empty()) tokens.push_back(current);
                current.clear();
            }
            tokens.push_back(std::string(1, c));
        } else {
            current += c;
        }
    }

    if (!current.empty()) {
        while (current.size() > 4) {
            tokens.push_back(current.substr(0, 4));
            current = current.substr(4);
        }
        if (!current.empty()) tokens.push_back(current);
    }

    return tokens;
}

} // namespace gateway

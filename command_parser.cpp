#include "command_parser.h"
#include <sstream>
#include <algorithm>
#include <cctype>

// Splits `s` on any whitespace, returning non-empty tokens.
std::vector<std::string> CommandParser::tokenize(const std::string& s) {
    std::vector<std::string> tokens;
    std::istringstream stream(s);
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

std::optional<ParsedCommand> CommandParser::parse(const std::string& raw) {
    // Strip trailing \r\n so we handle both \n and \r\n line endings
    std::string trimmed = raw;
    while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r')) {
        trimmed.pop_back();
    }

    auto tokens = tokenize(trimmed);
    if (tokens.empty()) return std::nullopt;

    ParsedCommand cmd;

    // Normalize command name to uppercase so "get" and "GET" both work
    cmd.name = tokens[0];
    std::transform(cmd.name.begin(), cmd.name.end(), cmd.name.begin(), ::toupper);

    // Everything after the command name is an argument
    for (size_t i = 1; i < tokens.size(); ++i) {
        cmd.args.push_back(tokens[i]);
    }

    return cmd;
}
#pragma once

#include <string>
#include <vector>
#include <optional>

// CommandParser handles the simple text protocol used by Mini-Redis.
//
// Protocol format (intentionally Redis-RESP-inspired but simpler):
//   SET <key> <value> [EX <milliseconds>]\r\n
//   GET <key>\r\n
//   DEL <key>\r\n
//   EXISTS <key>\r\n
//   PING\r\n
//
// Responses are plain text terminated with \r\n:
//   +OK\r\n          (success)
//   +PONG\r\n        (ping reply)
//   $<value>\r\n     (string value)
//   -ERR <msg>\r\n   (error)
//   :0\r\n / :1\r\n  (integer reply, used for DEL/EXISTS)
//   $-1\r\n          (nil, key not found)

struct ParsedCommand {
    std::string name;              // e.g. "SET", "GET"
    std::vector<std::string> args; // remaining tokens after the command name
};

class CommandParser {
public:
    // Parses a raw request string into a ParsedCommand.
    // Returns std::nullopt if the input is empty or malformed.
    static std::optional<ParsedCommand> parse(const std::string& raw);

private:
    // Splits the raw string on whitespace into tokens.
    static std::vector<std::string> tokenize(const std::string& s);
};
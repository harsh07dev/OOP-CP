#pragma once
#ifndef PROTOCOL_H
#define PROTOCOL_H

// protocol.h - Shared communication protocol definitions between server and client.
// Centralizes message framing delimiters, command strings, size limits, and handshake response tokens.

#include <cstddef>
#include <string_view>
#include <cctype>

namespace protocol {

// Wire framing delimiter for messages
inline constexpr char DELIMITER = '\n';
inline constexpr std::string_view DELIMITER_STR = "\n";

// Maximum permitted message size in bytes across the wire, including the trailing delimiter ('\n').
// Maximum payload text length is MAX_MESSAGE_SIZE - 1 bytes (2047 bytes).
inline constexpr std::size_t MAX_MESSAGE_SIZE = 2048;

// Maximum permitted username length in characters
inline constexpr std::size_t MAX_USERNAME_LENGTH = 32;

// Client command literals
inline constexpr std::string_view CMD_QUIT = "/quit";

// Registration response tokens
inline constexpr std::string_view RESP_OK = "OK";
inline constexpr std::string_view RESP_ERR_USERNAME_TAKEN = "ERR_USERNAME_TAKEN";
inline constexpr std::string_view RESP_ERR_INVALID_USERNAME = "ERR_INVALID_USERNAME";

// System announcement prefix
inline constexpr std::string_view PREFIX_SYSTEM = "[System]";

// Username validation rules:
// 1. Length must be between 1 and MAX_USERNAME_LENGTH characters.
// 2. Allowed characters: alphanumeric (a-z, A-Z, 0-9), underscore (_), and hyphen (-).
// 3. Must not contain spaces, control characters, or newlines.
inline bool isValidUsername(std::string_view username) noexcept {
    if (username.empty() || username.size() > MAX_USERNAME_LENGTH) {
        return false;
    }
    for (char c : username) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '_' && c != '-') {
            return false;
        }
    }
    return true;
}

} // namespace protocol

#endif // PROTOCOL_H

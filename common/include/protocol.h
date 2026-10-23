#pragma once
#ifndef PROTOCOL_H
#define PROTOCOL_H

// protocol.h - Shared communication protocol definitions between server and client.
// Centralizes message framing delimiters, command strings, size limits, and handshake response tokens.

#include <cstddef>
#include <string_view>

namespace protocol {

// Wire framing delimiter for messages
inline constexpr char DELIMITER = '\n';
inline constexpr std::string_view DELIMITER_STR = "\n";

// Maximum permitted message size in bytes across the wire
inline constexpr std::size_t MAX_MESSAGE_SIZE = 2048;

// Client command literals
inline constexpr std::string_view CMD_QUIT = "/quit";

// Registration response tokens (reserved for Phase 2 Task 2 handshake implementation)
inline constexpr std::string_view RESP_OK = "OK";
inline constexpr std::string_view RESP_ERR_USERNAME_TAKEN = "ERR_USERNAME_TAKEN";
inline constexpr std::string_view RESP_ERR_INVALID_USERNAME = "ERR_INVALID_USERNAME";

// System announcement prefix
inline constexpr std::string_view PREFIX_SYSTEM = "[System]";

} // namespace protocol

#endif // PROTOCOL_H

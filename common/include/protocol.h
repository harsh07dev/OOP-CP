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

// Maximum permitted message size in bytes across the wire
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

// Protocol versions
enum class ProtocolVersion : int {
    LEGACY_V0 = 0,
    STRUCTURED_V1 = 1
};

// Structured protocol version 1 constants
inline constexpr int CURRENT_VERSION = 1;

// Message type identifiers
inline constexpr std::string_view TYPE_REGISTER_REQ   = "register_req";
inline constexpr std::string_view TYPE_REGISTER_ACK   = "register_ack";
inline constexpr std::string_view TYPE_MSG_SEND       = "msg_send";
inline constexpr std::string_view TYPE_MSG_ACK        = "msg_ack";
inline constexpr std::string_view TYPE_MSG_BROADCAST  = "msg_broadcast";
inline constexpr std::string_view TYPE_MSG_DIRECT     = "msg_direct";     // Reserved for private messaging
inline constexpr std::string_view TYPE_SYSTEM_EVENT   = "system_event";
inline constexpr std::string_view TYPE_DISCONNECT_REQ = "disconnect_req";
inline constexpr std::string_view TYPE_ERROR          = "error";

// Status identifiers
inline constexpr std::string_view STATUS_OK    = "ok";
inline constexpr std::string_view STATUS_ERROR = "error";

// System event categories
inline constexpr std::string_view EVENT_JOIN   = "join";
inline constexpr std::string_view EVENT_LEAVE  = "leave";
inline constexpr std::string_view EVENT_SYSTEM = "system";

// Structured error codes
inline constexpr std::string_view ERR_MALFORMED_MESSAGE   = "ERR_MALFORMED_MESSAGE";
inline constexpr std::string_view ERR_UNSUPPORTED_VERSION = "ERR_UNSUPPORTED_VERSION";
inline constexpr std::string_view ERR_UNKNOWN_TYPE        = "ERR_UNKNOWN_TYPE";
inline constexpr std::string_view ERR_EMPTY_MESSAGE       = "ERR_EMPTY_MESSAGE";
inline constexpr std::string_view ERR_MESSAGE_TOO_LARGE   = "ERR_MESSAGE_TOO_LARGE";
inline constexpr std::string_view ERR_NOT_REGISTERED      = "ERR_NOT_REGISTERED";
inline constexpr std::string_view ERR_ALREADY_REGISTERED  = "ERR_ALREADY_REGISTERED";
inline constexpr std::string_view ERR_USERNAME_TAKEN      = "ERR_USERNAME_TAKEN";
inline constexpr std::string_view ERR_INVALID_USERNAME    = "ERR_INVALID_USERNAME";

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

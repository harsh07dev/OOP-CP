#ifndef PROTOCOL_HANDLER_H
#define PROTOCOL_HANDLER_H

// protocol_handler.h - Modular protocol parsing and formatting for the client.
// Decouples message framing and protocol serialization from networking and UI logic.

#include <string>

enum class MessageCategory {
    CHAT,
    SYSTEM,
    BROADCAST,
    UNKNOWN
};

struct ClientMessage {
    MessageCategory category;
    std::string sender;
    std::string text;
    std::string raw;
};

class ProtocolHandler {
public:
    // Formats username registration (JOIN message)
    static std::string format_join(const std::string& username) {
        return username + "\n";
    }

    // Formats outgoing chat messages (CHAT message)
    static std::string format_chat(const std::string& message) {
        return message + "\n";
    }

    // Formats graceful disconnection request (LEAVE message)
    static std::string format_leave() {
        return "/quit\n";
    }

    // Parses raw incoming server payload into structured ClientMessage
    static ClientMessage parse(const std::string& raw_data) {
        ClientMessage msg;
        msg.raw = raw_data;

        std::string trimmed = raw_data;
        while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r')) {
            trimmed.pop_back();
        }
        msg.text = trimmed;

        // Identify system messages versus regular user broadcasts
        if (trimmed.rfind("[System]", 0) == 0 || 
            trimmed.rfind("[Notice]", 0) == 0 || 
            trimmed.rfind("[Server]", 0) == 0 || 
            trimmed.rfind("***", 0) == 0) {
            msg.category = MessageCategory::SYSTEM;
        } else {
            msg.category = MessageCategory::BROADCAST;
        }

        return msg;
    }
};

#endif // PROTOCOL_HANDLER_H

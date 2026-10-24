#pragma once
#ifndef PROTOCOL_MESSAGE_H
#define PROTOCOL_MESSAGE_H

// protocol_message.h - Structured protocol message definition and serialization.
// Provides a structured representation for Phase 4 messages with versioning, correlation IDs,
// error reporting, and roadmap attributes (private messaging, rooms, history metadata).

#include "protocol.h"
#include "json_mini.h"

#include <string>
#include <string_view>
#include <cstdint>
#include <utility>

namespace protocol {

struct StructuredMessage {
    int version{CURRENT_VERSION};
    std::string type;
    std::string id;          // Client request or correlation ID
    std::string msgId;       // Server-assigned unique message ID
    std::string from;        // Sender username
    std::string to;          // Recipient username (future private messaging)
    std::string room;        // Room identifier (future chat rooms)
    std::string content;     // Chat or announcement content
    std::string status;      // "ok" or "error"
    std::string code;        // Standardized status or error code
    std::string detail;      // Human-readable detail or explanation
    std::string event;       // System event category ("join", "leave", "system")
    std::string username;    // Subject username for registration / user events
    int64_t timestamp{0};    // UTC timestamp seconds since epoch

    // Serialize to compact single-line JSON string without delimiter
    std::string serialize() const {
        json::JsonValue obj = json::JsonValue::object();
        obj.set("version", json::JsonValue(version));
        obj.set("type", json::JsonValue(type));

        if (!id.empty()) obj.set("id", json::JsonValue(id));
        if (!msgId.empty()) obj.set("msg_id", json::JsonValue(msgId));
        if (!from.empty()) obj.set("from", json::JsonValue(from));
        if (!to.empty()) obj.set("to", json::JsonValue(to));
        if (!room.empty()) obj.set("room", json::JsonValue(room));
        if (!content.empty()) obj.set("content", json::JsonValue(content));
        if (!status.empty()) obj.set("status", json::JsonValue(status));
        if (!code.empty()) obj.set("code", json::JsonValue(code));
        if (!detail.empty()) obj.set("detail", json::JsonValue(detail));
        if (!event.empty()) obj.set("event", json::JsonValue(event));
        if (!username.empty()) obj.set("username", json::JsonValue(username));
        if (timestamp != 0) obj.set("timestamp", json::JsonValue(timestamp));

        return obj.serialize();
    }

    // Serialize to wire format with trailing delimiter
    std::string serializeWire() const {
        return serialize() + std::string(DELIMITER_STR);
    }

    // Check if serialized wire format fits within maximum limit
    bool fitsInWireLimit(std::size_t maxWireLimit = MAX_MESSAGE_SIZE) const {
        return serializeWire().size() <= maxWireLimit;
    }

    // Adaptively truncate content so that the serialized JSON envelope strictly fits within wire limit
    void truncateToFitWire(std::size_t maxWireLimit = MAX_MESSAGE_SIZE) {
        if (serializeWire().size() <= maxWireLimit) {
            return;
        }
        while (!content.empty() && serializeWire().size() > maxWireLimit) {
            std::size_t excess = serializeWire().size() - maxWireLimit;
            std::size_t toRemove = (excess > content.size()) ? content.size() : excess;
            if (toRemove == 0) toRemove = 1;
            content.erase(content.size() - toRemove);
            // Ensure UTF-8 boundary integrity on trailing bytes
            while (!content.empty() && (static_cast<unsigned char>(content.back()) & 0xC0) == 0x80) {
                content.pop_back();
            }
            if (!content.empty() && (static_cast<unsigned char>(content.back()) & 0x80) != 0) {
                content.pop_back();
            }
        }
    }

    // Protocol detection heuristic: determines whether incoming text represents an attempted structured message
    static bool isAttemptedStructured(std::string_view line) noexcept {
        // Strip UTF-8 BOM if present
        if (line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line.remove_prefix(3);
        }
        // Trim leading whitespace
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t' || line.front() == '\r')) {
            line.remove_prefix(1);
        }
        if (line.empty()) {
            return false;
        }
        // Any line starting with '{' is an attempted JSON object
        if (line.front() == '{') {
            return true;
        }
        // Markers indicating explicit structured protocol intent
        if (line.find("\"version\"") != std::string_view::npos ||
            line.find("'version'") != std::string_view::npos ||
            line.find("\"type\"") != std::string_view::npos ||
            line.find("'type'") != std::string_view::npos ||
            line.find("register_req") != std::string_view::npos ||
            line.find("msg_send") != std::string_view::npos) {
            return true;
        }
        return false;
    }

    // Deserialize from JSON payload
    static bool deserialize(std::string_view jsonStr, StructuredMessage& outMsg, std::string& outErr) {
        // Strip UTF-8 BOM if present
        if (jsonStr.size() >= 3 &&
            static_cast<unsigned char>(jsonStr[0]) == 0xEF &&
            static_cast<unsigned char>(jsonStr[1]) == 0xBB &&
            static_cast<unsigned char>(jsonStr[2]) == 0xBF) {
            jsonStr.remove_prefix(3);
        }
        // Trim leading whitespace
        while (!jsonStr.empty() && (jsonStr.front() == ' ' || jsonStr.front() == '\t' || jsonStr.front() == '\r')) {
            jsonStr.remove_prefix(1);
        }
        // Trim trailing whitespace
        while (!jsonStr.empty() && (jsonStr.back() == ' ' || jsonStr.back() == '\t' || jsonStr.back() == '\r')) {
            jsonStr.remove_suffix(1);
        }

        json::JsonValue root;
        if (!json::JsonValue::parse(jsonStr, root, outErr)) {
            return false;
        }

        if (!root.isObject()) {
            outErr = "Root JSON value must be an object";
            return false;
        }

        outMsg = StructuredMessage{};
        outMsg.version = static_cast<int>(root.getInt("version", 0));
        outMsg.type = root.getString("type");
        outMsg.id = root.getString("id");
        outMsg.msgId = root.getString("msg_id");
        outMsg.from = root.getString("from");
        outMsg.to = root.getString("to");
        outMsg.room = root.getString("room");
        outMsg.content = root.getString("content");
        outMsg.status = root.getString("status");
        outMsg.code = root.getString("code");
        outMsg.detail = root.getString("detail");
        outMsg.event = root.getString("event");
        outMsg.username = root.getString("username");
        outMsg.timestamp = root.getInt("timestamp", 0);

        return true;
    }

    // Validate semantic correctness of the structured message
    bool validate(std::string& outErr) const {
        if (version != CURRENT_VERSION) {
            outErr = "Unsupported protocol version: " + std::to_string(version);
            return false;
        }
        if (type.empty()) {
            outErr = "Missing required 'type' field";
            return false;
        }

        if (type == TYPE_REGISTER_REQ) {
            if (username.empty()) {
                outErr = "Registration request requires non-empty 'username'";
                return false;
            }
            if (!isValidUsername(username)) {
                outErr = "Invalid username format";
                return false;
            }
        } else if (type == TYPE_REGISTER_ACK) {
            if (status != STATUS_OK) {
                outErr = "Registration acknowledgement requires status 'ok'";
                return false;
            }
            if (username.empty()) {
                outErr = "Registration acknowledgement requires 'username'";
                return false;
            }
        } else if (type == TYPE_MSG_SEND) {
            if (content.empty()) {
                outErr = "Message content cannot be empty";
                return false;
            }
        } else if (type == TYPE_MSG_ACK) {
            if (status != STATUS_OK) {
                outErr = "Message acknowledgement requires status 'ok'";
                return false;
            }
        } else if (type == TYPE_MSG_BROADCAST) {
            if (from.empty()) {
                outErr = "Broadcast message requires 'from'";
                return false;
            }
            if (content.empty()) {
                outErr = "Broadcast message requires 'content'";
                return false;
            }
        } else if (type == TYPE_SYSTEM_EVENT) {
            if (event.empty()) {
                outErr = "System event requires 'event' category";
                return false;
            }
        } else if (type == TYPE_ERROR) {
            if (code.empty()) {
                outErr = "Error message requires 'code'";
                return false;
            }
        }

        return true;
    }

    // Factory helper methods
    static StructuredMessage createRegisterReq(std::string_view username, std::string_view id = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_REGISTER_REQ);
        m.username = std::string(username);
        m.id = std::string(id);
        return m;
    }

    static StructuredMessage createRegisterAck(std::string_view username, std::string_view id = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_REGISTER_ACK);
        m.status = std::string(STATUS_OK);
        m.username = std::string(username);
        m.id = std::string(id);
        return m;
    }

    static StructuredMessage createMsgSend(std::string_view content, std::string_view id = "", std::string_view to = "", std::string_view room = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_MSG_SEND);
        m.content = std::string(content);
        m.id = std::string(id);
        m.to = std::string(to);
        m.room = std::string(room);
        return m;
    }

    static StructuredMessage createMsgAck(std::string_view msgId, std::string_view id = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_MSG_ACK);
        m.status = std::string(STATUS_OK);
        m.msgId = std::string(msgId);
        m.id = std::string(id);
        return m;
    }

    static StructuredMessage createMsgBroadcast(std::string_view from, std::string_view content, std::string_view msgId, int64_t timestamp, std::string_view to = "", std::string_view room = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_MSG_BROADCAST);
        m.from = std::string(from);
        m.content = std::string(content);
        m.msgId = std::string(msgId);
        m.timestamp = timestamp;
        m.to = std::string(to);
        m.room = std::string(room);
        return m;
    }

    static StructuredMessage createSystemEvent(std::string_view event, std::string_view username, std::string_view detail, int64_t timestamp) {
        StructuredMessage m;
        m.type = std::string(TYPE_SYSTEM_EVENT);
        m.event = std::string(event);
        m.username = std::string(username);
        m.detail = std::string(detail);
        m.timestamp = timestamp;
        return m;
    }

    static StructuredMessage createDisconnectReq(std::string_view id = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_DISCONNECT_REQ);
        m.id = std::string(id);
        return m;
    }

    static StructuredMessage createError(std::string_view code, std::string_view detail, std::string_view id = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_ERROR);
        m.status = std::string(STATUS_ERROR);
        m.code = std::string(code);
        m.detail = std::string(detail);
        m.id = std::string(id);
        return m;
    }
};

} // namespace protocol

#endif // PROTOCOL_MESSAGE_H

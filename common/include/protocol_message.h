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

struct HistoryItem {
    std::string msgId;
    std::string type;       // "public", "direct", "room"
    std::string from;
    std::string to;
    std::string room;
    std::string content;
    int64_t timestamp{0};
};

struct StructuredMessage {
    int version{CURRENT_VERSION};
    std::string type;
    std::string id;          // Client request or correlation ID
    std::string msgId;       // Server-assigned unique message ID
    std::string from;        // Sender username
    std::string to;          // Recipient username (private messaging)
    std::string room;        // Room identifier (chat rooms)
    std::string content;     // Chat or announcement content
    std::string status;      // "ok" or "error"
    std::string code;        // Standardized status or error code
    std::string detail;      // Human-readable detail or explanation
    std::string event;       // System event category ("join", "leave", "system")
    std::string username;    // Subject username for registration / user events
    std::vector<std::string> users;   // Active username list for user_list_resp
    std::vector<std::string> rooms;   // Active room list for room_list_resp
    std::vector<std::string> members; // Active member list for room_members_resp
    int count{0};            // Number of items in this payload page
    int totalUsers{0};       // Total online user count across all pages
    int totalRooms{0};       // Total room count across all pages
    int totalMembers{0};     // Total room member count across all pages
    int page{0};             // Page number (1-indexed)
    int limit{0};            // Page limit requested
    int totalPages{0};       // Total pages calculated
    int64_t timestamp{0};    // UTC timestamp seconds since epoch

    // History protocol fields
    std::string conversationType;          // "public", "direct", "room"
    std::string withUser;                  // Peer username for direct conversation history
    std::string beforeId;                  // Message ID cursor for history pagination (older messages)
    std::string afterId;                   // Message ID cursor for history pagination (newer messages)
    bool hasMore{false};                   // Indicates more history messages available
    std::vector<HistoryItem> historyItems; // Retrieved history items for history_resp

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

        if (page > 0) obj.set("page", json::JsonValue(page));
        if (limit > 0) obj.set("limit", json::JsonValue(limit));
        if (totalPages > 0) obj.set("total_pages", json::JsonValue(totalPages));
        if (totalUsers > 0) obj.set("total_users", json::JsonValue(totalUsers));
        if (totalRooms > 0) obj.set("total_rooms", json::JsonValue(totalRooms));
        if (totalMembers > 0) obj.set("total_members", json::JsonValue(totalMembers));

        if (!conversationType.empty()) obj.set("conversation_type", json::JsonValue(conversationType));
        if (!withUser.empty()) obj.set("with", json::JsonValue(withUser));
        if (!beforeId.empty()) obj.set("before_id", json::JsonValue(beforeId));
        if (!afterId.empty()) obj.set("after_id", json::JsonValue(afterId));

        if (!users.empty() || type == TYPE_USER_LIST_RESP) {
            json::JsonValue arr = json::JsonValue::array();
            for (const auto& u : users) {
                arr.arrVal.emplace_back(u);
            }
            obj.set("users", arr);
            int64_t c = (count > 0) ? count : static_cast<int64_t>(users.size());
            obj.set("count", json::JsonValue(c));
        }

        if (!rooms.empty() || type == TYPE_ROOM_LIST_RESP) {
            json::JsonValue arr = json::JsonValue::array();
            for (const auto& r : rooms) {
                arr.arrVal.emplace_back(r);
            }
            obj.set("rooms", arr);
            int64_t c = (count > 0) ? count : static_cast<int64_t>(rooms.size());
            obj.set("count", json::JsonValue(c));
        }

        if (!members.empty() || type == TYPE_ROOM_MEMBERS_RESP) {
            json::JsonValue arr = json::JsonValue::array();
            for (const auto& m : members) {
                arr.arrVal.emplace_back(m);
            }
            obj.set("members", arr);
            int64_t c = (count > 0) ? count : static_cast<int64_t>(members.size());
            obj.set("count", json::JsonValue(c));
        }

        if (type == TYPE_HISTORY_RESP || !historyItems.empty()) {
            obj.set("has_more", json::JsonValue(hasMore));
            json::JsonValue arr = json::JsonValue::array();
            for (const auto& item : historyItems) {
                json::JsonValue m = json::JsonValue::object();
                m.set("id", json::JsonValue(item.msgId));
                m.set("msg_id", json::JsonValue(item.msgId));
                m.set("type", json::JsonValue(item.type));
                m.set("from", json::JsonValue(item.from));
                if (!item.to.empty()) m.set("to", json::JsonValue(item.to));
                if (!item.room.empty()) m.set("room", json::JsonValue(item.room));
                m.set("content", json::JsonValue(item.content));
                m.set("timestamp", json::JsonValue(item.timestamp));
                arr.arrVal.push_back(std::move(m));
            }
            obj.set("messages", arr);
            int64_t c = (count > 0) ? count : static_cast<int64_t>(historyItems.size());
            obj.set("count", json::JsonValue(c));
        }

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
            line.find("msg_send") != std::string_view::npos ||
            line.find("msg_direct") != std::string_view::npos ||
            line.find("user_list_req") != std::string_view::npos ||
            line.find("user_list_resp") != std::string_view::npos ||
            line.find("user_joined") != std::string_view::npos ||
            line.find("user_left") != std::string_view::npos ||
            line.find("room_create") != std::string_view::npos ||
            line.find("room_join") != std::string_view::npos ||
            line.find("room_leave") != std::string_view::npos ||
            line.find("room_list") != std::string_view::npos ||
            line.find("room_members") != std::string_view::npos ||
            line.find("room_msg") != std::string_view::npos ||
            line.find("history_req") != std::string_view::npos ||
            line.find("history_resp") != std::string_view::npos ||
            line.find("conversation_type") != std::string_view::npos) {
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

        outMsg.page = static_cast<int>(root.getInt("page", 0));
        outMsg.limit = static_cast<int>(root.getInt("limit", 0));
        outMsg.totalPages = static_cast<int>(root.getInt("total_pages", 0));
        outMsg.totalUsers = static_cast<int>(root.getInt("total_users", 0));
        outMsg.totalRooms = static_cast<int>(root.getInt("total_rooms", 0));
        outMsg.totalMembers = static_cast<int>(root.getInt("total_members", 0));
        outMsg.count = static_cast<int>(root.getInt("count", 0));

        outMsg.conversationType = root.getString("conversation_type");
        outMsg.withUser = root.getString("with");
        outMsg.beforeId = root.getString("before_id");
        outMsg.afterId = root.getString("after_id");
        outMsg.hasMore = root.getBool("has_more", false);

        const auto& usersVal = root.get("users");
        if (usersVal.isArray()) {
            for (const auto& item : usersVal.arrVal) {
                if (item.isString()) {
                    outMsg.users.push_back(item.strVal);
                }
            }
        }
        if (outMsg.count == 0 && !outMsg.users.empty()) {
            outMsg.count = static_cast<int>(outMsg.users.size());
        }

        const auto& roomsVal = root.get("rooms");
        if (roomsVal.isArray()) {
            for (const auto& item : roomsVal.arrVal) {
                if (item.isString()) {
                    outMsg.rooms.push_back(item.strVal);
                }
            }
        }
        if (outMsg.count == 0 && !outMsg.rooms.empty()) {
            outMsg.count = static_cast<int>(outMsg.rooms.size());
        }

        const auto& membersVal = root.get("members");
        if (membersVal.isArray()) {
            for (const auto& item : membersVal.arrVal) {
                if (item.isString()) {
                    outMsg.members.push_back(item.strVal);
                }
            }
        }
        if (outMsg.count == 0 && !outMsg.members.empty()) {
            outMsg.count = static_cast<int>(outMsg.members.size());
        }

        const auto& histVal = root.get("messages");
        if (histVal.isArray()) {
            for (const auto& item : histVal.arrVal) {
                if (item.isObject()) {
                    HistoryItem hi;
                    hi.msgId = item.getString("id");
                    if (hi.msgId.empty()) {
                        hi.msgId = item.getString("msg_id");
                    }
                    hi.type = item.getString("type");
                    hi.from = item.getString("from");
                    hi.to = item.getString("to");
                    hi.room = item.getString("room");
                    hi.content = item.getString("content");
                    hi.timestamp = item.getInt("timestamp", 0);
                    outMsg.historyItems.push_back(std::move(hi));
                }
            }
        }
        if (outMsg.count == 0 && !outMsg.historyItems.empty()) {
            outMsg.count = static_cast<int>(outMsg.historyItems.size());
        }

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
        } else if (type == TYPE_MSG_DIRECT_REQ) {
            if (to.empty()) {
                outErr = "Direct message requires 'to' recipient";
                return false;
            }
            if (content.empty()) {
                outErr = "Direct message content cannot be empty";
                return false;
            }
        } else if (type == TYPE_MSG_DIRECT) {
            if (from.empty() || to.empty()) {
                outErr = "Direct message requires 'from' and 'to'";
                return false;
            }
            if (content.empty()) {
                outErr = "Direct message requires 'content'";
                return false;
            }
        } else if (type == TYPE_ROOM_CREATE_REQ) {
            if (room.empty()) {
                outErr = "Room creation requires 'room' name";
                return false;
            }
            if (!isValidRoomName(room)) {
                outErr = "Invalid room name format";
                return false;
            }
        } else if (type == TYPE_ROOM_JOIN_REQ) {
            if (room.empty()) {
                outErr = "Room join requires 'room' name";
                return false;
            }
        } else if (type == TYPE_ROOM_LEAVE_REQ) {
            if (room.empty()) {
                outErr = "Room leave requires 'room' name";
                return false;
            }
        } else if (type == TYPE_ROOM_MSG_REQ) {
            if (room.empty()) {
                outErr = "Room message requires 'room' name";
                return false;
            }
            if (content.empty()) {
                outErr = "Room message content cannot be empty";
                return false;
            }
        } else if (type == TYPE_ROOM_LIST_REQ) {
            if (page < 0) {
                outErr = "Page number cannot be negative";
                return false;
            }
            if (limit < 0) {
                outErr = "Limit cannot be negative";
                return false;
            }
        } else if (type == TYPE_ROOM_MEMBERS_REQ) {
            if (room.empty()) {
                outErr = "Room members request requires 'room' name";
                return false;
            }
            if (page < 0) {
                outErr = "Page number cannot be negative";
                return false;
            }
            if (limit < 0) {
                outErr = "Limit cannot be negative";
                return false;
            }
        } else if (type == TYPE_HISTORY_REQ) {
            if (conversationType.empty()) {
                outErr = "History request requires 'conversation_type'";
                return false;
            }
            if (conversationType != CONV_PUBLIC &&
                conversationType != CONV_DIRECT &&
                conversationType != CONV_ROOM) {
                outErr = "Invalid 'conversation_type': must be 'public', 'direct', or 'room'";
                return false;
            }
            if (conversationType == CONV_DIRECT && withUser.empty()) {
                outErr = "Direct history request requires 'with' participant";
                return false;
            }
            if (conversationType == CONV_ROOM && room.empty()) {
                outErr = "Room history request requires 'room' name";
                return false;
            }
            if (limit < 0) {
                outErr = "Limit cannot be negative";
                return false;
            }
        } else if (type == TYPE_HISTORY_RESP) {
            if (status.empty()) {
                outErr = "History response requires 'status'";
                return false;
            }
        } else if (type == TYPE_SYSTEM_EVENT) {
            if (event.empty()) {
                outErr = "System event requires 'event' category";
                return false;
            }
        } else if (type == TYPE_USER_LIST_REQ) {
            if (page < 0) {
                outErr = "Page number cannot be negative";
                return false;
            }
            if (limit < 0) {
                outErr = "Limit cannot be negative";
                return false;
            }
        } else if (type == TYPE_USER_LIST_RESP) {
            if (status != STATUS_OK) {
                outErr = "User list response requires status 'ok'";
                return false;
            }
        } else if (type == TYPE_USER_JOINED || type == TYPE_USER_LEFT) {
            if (username.empty()) {
                outErr = "User join/leave event requires non-empty 'username'";
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

    static StructuredMessage createMsgDirectReq(std::string_view to, std::string_view content, std::string_view id = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_MSG_DIRECT_REQ);
        m.to = std::string(to);
        m.content = std::string(content);
        m.id = std::string(id);
        return m;
    }

    static StructuredMessage createMsgDirectAck(std::string_view msgId, std::string_view to, int64_t timestamp, std::string_view id = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_MSG_DIRECT_ACK);
        m.status = std::string(STATUS_OK);
        m.msgId = std::string(msgId);
        m.to = std::string(to);
        m.timestamp = timestamp;
        m.id = std::string(id);
        return m;
    }

    static StructuredMessage createMsgDirect(std::string_view from, std::string_view to, std::string_view content, std::string_view msgId, int64_t timestamp) {
        StructuredMessage m;
        m.type = std::string(TYPE_MSG_DIRECT);
        m.from = std::string(from);
        m.to = std::string(to);
        m.content = std::string(content);
        m.msgId = std::string(msgId);
        m.timestamp = timestamp;
        return m;
    }

    static StructuredMessage createRoomCreateReq(std::string_view room, std::string_view id = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_ROOM_CREATE_REQ);
        m.room = std::string(room);
        m.id = std::string(id);
        return m;
    }

    static StructuredMessage createRoomCreateResp(std::string_view room, std::string_view status = STATUS_OK, std::string_view id = "", std::string_view detail = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_ROOM_CREATE_RESP);
        m.room = std::string(room);
        m.status = std::string(status);
        m.id = std::string(id);
        m.detail = std::string(detail);
        return m;
    }

    static StructuredMessage createRoomJoinReq(std::string_view room, std::string_view id = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_ROOM_JOIN_REQ);
        m.room = std::string(room);
        m.id = std::string(id);
        return m;
    }

    static StructuredMessage createRoomJoinResp(std::string_view room, std::string_view status = STATUS_OK, std::string_view id = "", std::string_view detail = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_ROOM_JOIN_RESP);
        m.room = std::string(room);
        m.status = std::string(status);
        m.id = std::string(id);
        m.detail = std::string(detail);
        return m;
    }

    static StructuredMessage createRoomLeaveReq(std::string_view room, std::string_view id = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_ROOM_LEAVE_REQ);
        m.room = std::string(room);
        m.id = std::string(id);
        return m;
    }

    static StructuredMessage createRoomLeaveResp(std::string_view room, std::string_view status = STATUS_OK, std::string_view id = "", std::string_view detail = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_ROOM_LEAVE_RESP);
        m.room = std::string(room);
        m.status = std::string(status);
        m.id = std::string(id);
        m.detail = std::string(detail);
        return m;
    }

    static StructuredMessage createRoomListReq(std::string_view id = "", int page = 1, int limit = DEFAULT_ROOM_LIST_LIMIT) {
        StructuredMessage m;
        m.type = std::string(TYPE_ROOM_LIST_REQ);
        m.id = std::string(id);
        m.page = page;
        m.limit = limit;
        return m;
    }

    static StructuredMessage createRoomListResp(const std::vector<std::string>& roomList, int totalRooms, int page, int totalPages, std::string_view id = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_ROOM_LIST_RESP);
        m.status = std::string(STATUS_OK);
        m.rooms = roomList;
        m.count = static_cast<int>(roomList.size());
        m.totalRooms = totalRooms;
        m.page = page;
        m.totalPages = totalPages;
        m.id = std::string(id);
        return m;
    }

    static StructuredMessage createRoomMembersReq(std::string_view room, std::string_view id = "", int page = 1, int limit = DEFAULT_ROOM_MEMBERS_LIMIT) {
        StructuredMessage m;
        m.type = std::string(TYPE_ROOM_MEMBERS_REQ);
        m.room = std::string(room);
        m.id = std::string(id);
        m.page = page;
        m.limit = limit;
        return m;
    }

    static StructuredMessage createRoomMembersResp(std::string_view room, const std::vector<std::string>& memberList, int totalMembers, int page, int totalPages, std::string_view id = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_ROOM_MEMBERS_RESP);
        m.status = std::string(STATUS_OK);
        m.room = std::string(room);
        m.members = memberList;
        m.count = static_cast<int>(memberList.size());
        m.totalMembers = totalMembers;
        m.page = page;
        m.totalPages = totalPages;
        m.id = std::string(id);
        return m;
    }

    static StructuredMessage createRoomMsgReq(std::string_view room, std::string_view content, std::string_view id = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_ROOM_MSG_REQ);
        m.room = std::string(room);
        m.content = std::string(content);
        m.id = std::string(id);
        return m;
    }

    static StructuredMessage createRoomMsgAck(std::string_view msgId, std::string_view room, int64_t timestamp, std::string_view id = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_ROOM_MSG_ACK);
        m.status = std::string(STATUS_OK);
        m.msgId = std::string(msgId);
        m.room = std::string(room);
        m.timestamp = timestamp;
        m.id = std::string(id);
        return m;
    }

    static StructuredMessage createRoomMsg(std::string_view from, std::string_view room, std::string_view content, std::string_view msgId, int64_t timestamp) {
        StructuredMessage m;
        m.type = std::string(TYPE_ROOM_MSG);
        m.from = std::string(from);
        m.room = std::string(room);
        m.content = std::string(content);
        m.msgId = std::string(msgId);
        m.timestamp = timestamp;
        return m;
    }

    static StructuredMessage createRoomUserJoined(std::string_view room, std::string_view username, int64_t timestamp = 0) {
        StructuredMessage m;
        m.type = std::string(TYPE_ROOM_USER_JOINED);
        m.room = std::string(room);
        m.username = std::string(username);
        m.event = std::string(EVENT_JOIN);
        m.timestamp = timestamp;
        m.detail = "[Room] " + std::string(username) + " has joined " + std::string(room) + ".";
        return m;
    }

    static StructuredMessage createRoomUserLeft(std::string_view room, std::string_view username, int64_t timestamp = 0) {
        StructuredMessage m;
        m.type = std::string(TYPE_ROOM_USER_LEFT);
        m.room = std::string(room);
        m.username = std::string(username);
        m.event = std::string(EVENT_LEAVE);
        m.timestamp = timestamp;
        m.detail = "[Room] " + std::string(username) + " has left " + std::string(room) + ".";
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

    static StructuredMessage createUserListReq(std::string_view id = "", int page = 1, int limit = DEFAULT_USER_LIST_LIMIT) {
        StructuredMessage m;
        m.type = std::string(TYPE_USER_LIST_REQ);
        m.id = std::string(id);
        m.page = page;
        m.limit = limit;
        return m;
    }

    static StructuredMessage createUserListResp(const std::vector<std::string>& userList, int totalUsers, int page, int totalPages, std::string_view id = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_USER_LIST_RESP);
        m.status = std::string(STATUS_OK);
        m.users = userList;
        m.count = static_cast<int>(userList.size());
        m.totalUsers = totalUsers;
        m.page = page;
        m.totalPages = totalPages;
        m.id = std::string(id);
        return m;
    }

    static StructuredMessage createUserJoined(std::string_view username, int64_t timestamp = 0, std::string_view id = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_USER_JOINED);
        m.username = std::string(username);
        m.event = std::string(EVENT_JOIN);
        m.timestamp = timestamp;
        m.id = std::string(id);
        return m;
    }

    static StructuredMessage createUserLeft(std::string_view username, int64_t timestamp = 0, std::string_view id = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_USER_LEFT);
        m.username = std::string(username);
        m.event = std::string(EVENT_LEAVE);
        m.timestamp = timestamp;
        m.id = std::string(id);
        return m;
    }

    static StructuredMessage createHistoryReq(std::string_view conversationType,
                                             std::string_view withUser = "",
                                             std::string_view room = "",
                                             int limit = DEFAULT_HISTORY_LIMIT,
                                             std::string_view beforeId = "",
                                             std::string_view afterId = "",
                                             std::string_view id = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_HISTORY_REQ);
        m.id = std::string(id);
        m.conversationType = std::string(conversationType);
        m.withUser = std::string(withUser);
        m.room = std::string(room);
        m.limit = limit;
        m.beforeId = std::string(beforeId);
        m.afterId = std::string(afterId);
        return m;
    }

    static StructuredMessage createHistoryResp(std::string_view conversationType,
                                              std::string_view withUser,
                                              std::string_view room,
                                              const std::vector<HistoryItem>& items,
                                              bool hasMore,
                                              std::string_view id = "") {
        StructuredMessage m;
        m.type = std::string(TYPE_HISTORY_RESP);
        m.id = std::string(id);
        m.status = std::string(STATUS_OK);
        m.conversationType = std::string(conversationType);
        m.withUser = std::string(withUser);
        m.room = std::string(room);
        m.historyItems = items;
        m.count = static_cast<int>(items.size());
        m.hasMore = hasMore;
        return m;
    }

    bool isUserJoinEvent() const noexcept {
        return type == TYPE_USER_JOINED || (type == TYPE_SYSTEM_EVENT && event == EVENT_JOIN);
    }

    bool isUserLeaveEvent() const noexcept {
        return type == TYPE_USER_LEFT || (type == TYPE_SYSTEM_EVENT && event == EVENT_LEAVE);
    }
};

} // namespace protocol

#endif // PROTOCOL_MESSAGE_H

// chat_session.cpp - Interactive structured-protocol client behavior.
// Converts console commands into Structured Protocol v1 requests and renders
// incoming JSON events in a human-friendly console format.

#include "chat_session.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iostream>
#include <sstream>
#include <vector>

namespace {

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::vector<std::string> splitWords(const std::string& text) {
    std::vector<std::string> words;
    std::istringstream stream(text);
    std::string word;
    while (stream >> word) {
        words.push_back(word);
    }
    return words;
}

std::string restAfterFirstWord(const std::string& text) {
    std::size_t pos = text.find_first_of(" \t");
    if (pos == std::string::npos) {
        return "";
    }
    return trim(text.substr(pos + 1));
}

} // namespace

ChatSession::ChatSession(Client& client)
    : client_(client) {
}

std::string ChatSession::nextRequestId(const char* prefix) {
    std::uint64_t value = ++requestCounter_;
    return std::string(prefix) + "-" + std::to_string(value);
}

void ChatSession::start() {
    running_ = true;
}

void ChatSession::stop() {
    running_ = false;
}

bool ChatSession::isRunning() const {
    return running_;
}

bool ChatSession::isRegistered() const {
    return registered_;
}

const std::string& ChatSession::username() const {
    return username_;
}

ChatSession::RegistrationOutcome ChatSession::registerUser(const std::string& username, std::string& detail) {
    username_.clear();
    registered_ = false;
    detail.clear();

    auto request = protocol::StructuredMessage::createRegisterReq(
        username, nextRequestId("reg"));
    if (!sendStructured(request)) {
        return RegistrationOutcome::ConnectionFailed;
    }

    while (client_.is_connected()) {
        std::string line;
        if (!client_.receive_line(line)) {
            return RegistrationOutcome::ConnectionFailed;
        }
        if (line.empty()) {
            continue;
        }

        protocol::StructuredMessage message;
        std::string parseError;
        if (!protocol::StructuredMessage::deserialize(line, message, parseError)) {
            // Tolerate a legacy "OK" response for defensive compatibility.
            if (line == protocol::RESP_OK) {
                username_ = username;
                registered_ = true;
                return RegistrationOutcome::Success;
            }
            printPlain("[Warning] Ignoring malformed server frame during registration.");
            continue;
        }

        if (message.type == protocol::TYPE_REGISTER_ACK) {
            username_ = username;
            registered_ = true;
            return RegistrationOutcome::Success;
        }

        if (message.type == protocol::TYPE_ERROR) {
            detail = message.code;
            if (!message.detail.empty()) {
                detail += ": " + message.detail;
            }
            if (message.code == protocol::ERR_USERNAME_TAKEN) {
                return RegistrationOutcome::UsernameTaken;
            }
            if (message.code == protocol::ERR_INVALID_USERNAME) {
                return RegistrationOutcome::InvalidUsername;
            }
            return RegistrationOutcome::UnexpectedError;
        }

        // System events may be queued from other clients; show them and keep
        // waiting for the registration acknowledgement.
        dispatchServerMessage(message);
    }

    return RegistrationOutcome::ConnectionFailed;
}

bool ChatSession::handleCommand(const std::string& line) {
    std::string trimmed = trim(line);
    if (trimmed.empty()) {
        return true;
    }

    // Plain text is a public chat message.
    if (trimmed.front() != '/') {
        const std::string requestId = nextRequestId("msg");
        auto message = protocol::StructuredMessage::createMsgSend(trimmed, requestId);
        rememberPending(requestId, EchoKind::Public, "", trimmed);
        if (!sendStructured(message)) {
            discardPending(requestId);
        }
        return true;
    }

    std::vector<std::string> words = splitWords(trimmed);
    if (words.empty()) {
        return true;
    }
    const std::string command = lower(words[0]);

    if (command == "/quit" || command == "/exit") {
        auto request = protocol::StructuredMessage::createDisconnectReq(nextRequestId("req"));
        sendStructured(request, false);
        printPlain("Disconnecting from server...");
        stop();
        return false;
    }

    if (command == "/help" || command == "/commands" || command == "/?") {
        printHelp();
        return true;
    }

    if (command == "/whoami") {
        printPlain("You are logged in as " + username_ + ".");
        return true;
    }

    if (command == "/status") {
        printPlain(std::string("User: ") + username_ +
                   " | Server: " + client_.get_server_ip() + ":" +
                   std::to_string(client_.get_server_port()) +
                   " | Connected: " + (client_.is_connected() ? "yes" : "no"));
        return true;
    }

    if (command == "/users") {
        int page = 1;
        int limit = protocol::DEFAULT_USER_LIST_LIMIT;
        if (words.size() > 3 ||
            (words.size() >= 2 && !parsePositiveInt(words[1], page)) ||
            (words.size() >= 3 && !parsePositiveInt(words[2], limit))) {
            printPlain("Usage: /users [page] [limit]");
            return true;
        }
        sendStructured(protocol::StructuredMessage::createUserListReq(
            nextRequestId("req"), page, limit));
        return true;
    }

    if (command == "/msg" || command == "/dm") {
        std::string rest = restAfterFirstWord(trimmed);
        std::size_t split = rest.find_first_of(" \t");
        if (split == std::string::npos) {
            printPlain("Usage: /msg <username> <message>");
            return true;
        }
        std::string recipient = rest.substr(0, split);
        std::string content = trim(rest.substr(split + 1));
        if (recipient.empty() || content.empty()) {
            printPlain("Usage: /msg <username> <message>");
            return true;
        }
        auto request = protocol::StructuredMessage::createMsgDirectReq(
            recipient, content, nextRequestId("dm"));
        const std::string requestId = request.id;
        rememberPending(requestId, EchoKind::Direct, recipient, content);
        if (!sendStructured(request)) {
            discardPending(requestId);
        }
        return true;
    }

    if (command == "/create" || command == "/join" || command == "/leave") {
        if (words.size() != 2) {
            printPlain("Usage: " + words[0] + " <room>");
            return true;
        }
        std::string id = nextRequestId("room");
        protocol::StructuredMessage request;
        if (command == "/create") {
            request = protocol::StructuredMessage::createRoomCreateReq(words[1], id);
        } else if (command == "/join") {
            request = protocol::StructuredMessage::createRoomJoinReq(words[1], id);
        } else {
            request = protocol::StructuredMessage::createRoomLeaveReq(words[1], id);
        }
        sendStructured(request);
        return true;
    }

    if (command == "/rooms") {
        int page = 1;
        int limit = protocol::DEFAULT_ROOM_LIST_LIMIT;
        if (words.size() > 3 ||
            (words.size() >= 2 && !parsePositiveInt(words[1], page)) ||
            (words.size() >= 3 && !parsePositiveInt(words[2], limit))) {
            printPlain("Usage: /rooms [page] [limit]");
            return true;
        }
        sendStructured(protocol::StructuredMessage::createRoomListReq(
            nextRequestId("req"), page, limit));
        return true;
    }

    if (command == "/members") {
        if (words.size() < 2 || words.size() > 4) {
            printPlain("Usage: /members <room> [page] [limit]");
            return true;
        }
        int page = 1;
        int limit = protocol::DEFAULT_ROOM_MEMBERS_LIMIT;
        if ((words.size() >= 3 && !parsePositiveInt(words[2], page)) ||
            (words.size() >= 4 && !parsePositiveInt(words[3], limit))) {
            printPlain("Usage: /members <room> [page] [limit]");
            return true;
        }
        sendStructured(protocol::StructuredMessage::createRoomMembersReq(
            words[1], nextRequestId("req"), page, limit));
        return true;
    }

    if (command == "/room" || command == "/rmsg") {
        std::string rest = restAfterFirstWord(trimmed);
        std::size_t split = rest.find_first_of(" \t");
        if (split == std::string::npos) {
            printPlain("Usage: /room <room> <message>");
            return true;
        }
        std::string room = rest.substr(0, split);
        std::string content = trim(rest.substr(split + 1));
        if (room.empty() || content.empty()) {
            printPlain("Usage: /room <room> <message>");
            return true;
        }
        auto request = protocol::StructuredMessage::createRoomMsgReq(
            room, content, nextRequestId("rmsg"));
        const std::string requestId = request.id;
        rememberPending(requestId, EchoKind::Room, room, content);
        if (!sendStructured(request)) {
            discardPending(requestId);
        }
        return true;
    }

    if (command == "/history") {
        std::string conversation = std::string(protocol::CONV_PUBLIC);
        std::string withUser;
        std::string room;
        std::string beforeId;
        std::string afterId;
        int limit = protocol::DEFAULT_HISTORY_LIMIT;
        std::size_t index = 1;

        if (words.size() > 1) {
            std::string mode = lower(words[1]);
            if (mode == "public") {
                index = 2;
            } else if (mode == "dm" || mode == "direct") {
                if (words.size() < 3) {
                    printPlain("Usage: /history dm <username> [limit] [before|after <msg-id>]");
                    return true;
                }
                conversation = std::string(protocol::CONV_DIRECT);
                withUser = words[2];
                index = 3;
            } else if (mode == "room") {
                if (words.size() < 3) {
                    printPlain("Usage: /history room <room> [limit] [before|after <msg-id>]");
                    return true;
                }
                conversation = std::string(protocol::CONV_ROOM);
                room = words[2];
                index = 3;
            } else {
                conversation = std::string(protocol::CONV_DIRECT);
                withUser = words[1];
                index = 2;
            }
        }

        for (; index < words.size(); ++index) {
            std::string token = lower(words[index]);
            if ((token == "before" || token == "after") && index + 1 < words.size()) {
                if (token == "before") {
                    beforeId = words[++index];
                } else {
                    afterId = words[++index];
                }
            } else if (parsePositiveInt(words[index], limit)) {
                // limit parsed in place
            } else {
                printPlain("Usage: /history [public|dm <user>|room <room>] [limit] [before|after <msg-id>]");
                return true;
            }
        }

        sendStructured(protocol::StructuredMessage::createHistoryReq(
            conversation, withUser, room, limit, beforeId, afterId,
            nextRequestId("hist")));
        return true;
    }

    printPlain("[Error] Unknown command '" + words[0] + "'. Type /help for available commands.");
    return true;
}

void ChatSession::handleServerLine(const std::string& line) {
    if (line.empty()) {
        return;
    }

    protocol::StructuredMessage message;
    std::string parseError;
    if (!protocol::StructuredMessage::deserialize(line, message, parseError)) {
        printIncoming("[Server] " + line);
        return;
    }
    dispatchServerMessage(message);
}

void ChatSession::dispatchServerMessage(const protocol::StructuredMessage& message) {
    if (message.type == protocol::TYPE_REGISTER_ACK) {
        registered_ = true;
        return;
    }

    if (message.type == protocol::TYPE_MSG_BROADCAST) {
        printIncoming("[" + message.from + "] " + message.content);
    } else if (message.type == protocol::TYPE_MSG_DIRECT) {
        printIncoming("[DM from " + message.from + "] " + message.content);
    } else if (message.type == protocol::TYPE_MSG_ACK) {
        PendingEcho pending;
        if (takePending(message.id, EchoKind::Public, pending)) {
            printIncoming("[You] " + pending.content);
        }
    } else if (message.type == protocol::TYPE_MSG_DIRECT_ACK) {
        PendingEcho pending;
        if (takePending(message.id, EchoKind::Direct, pending)) {
            printIncoming("[DM to " + pending.target + "] " + pending.content);
        }
    } else if (message.type == protocol::TYPE_ROOM_MSG_ACK) {
        PendingEcho pending;
        if (takePending(message.id, EchoKind::Room, pending)) {
            printIncoming("[You @" + pending.target + "] " + pending.content);
        }
    } else if (message.type == protocol::TYPE_SYSTEM_EVENT ||
               message.type == protocol::TYPE_USER_JOINED ||
               message.type == protocol::TYPE_USER_LEFT) {
        if (!message.detail.empty()) {
            printIncoming(message.detail);
        } else if (message.isUserJoinEvent()) {
            printIncoming("[System] " + message.username + " has joined the chat.");
        } else if (message.isUserLeaveEvent()) {
            printIncoming("[System] " + message.username + " has left the chat.");
        }
    } else if (message.type == protocol::TYPE_USER_LIST_RESP) {
        std::string output = "--- Online users (page " + std::to_string(message.page) +
                             "/" + std::to_string(message.totalPages) +
                             ", total " + std::to_string(message.totalUsers) + ") ---\n";
        if (message.users.empty()) {
            output += "(no users on this page)\n";
        } else {
            for (const auto& user : message.users) {
                output += "  " + user + "\n";
            }
        }
        printIncoming(output);
    } else if (message.type == protocol::TYPE_ROOM_CREATE_RESP) {
        if (message.status == protocol::STATUS_OK) {
            printIncoming("[Room] Created room '" + message.room + "'.");
        } else {
            printIncoming("[Error] Could not create room '" + message.room + "': " + message.detail);
        }
    } else if (message.type == protocol::TYPE_ROOM_JOIN_RESP) {
        if (message.status == protocol::STATUS_OK) {
            printIncoming("[Room] Joined '" + message.room + "'.");
        } else {
            printIncoming("[Error] Could not join room '" + message.room + "': " + message.detail);
        }
    } else if (message.type == protocol::TYPE_ROOM_LEAVE_RESP) {
        if (message.status == protocol::STATUS_OK) {
            printIncoming("[Room] Left '" + message.room + "'.");
        } else {
            printIncoming("[Error] Could not leave room '" + message.room + "': " + message.detail);
        }
    } else if (message.type == protocol::TYPE_ROOM_LIST_RESP) {
        std::string output = "--- Rooms (page " + std::to_string(message.page) +
                             "/" + std::to_string(message.totalPages) +
                             ", total " + std::to_string(message.totalRooms) + ") ---\n";
        if (message.rooms.empty()) {
            output += "(no rooms on this page)\n";
        } else {
            for (const auto& room : message.rooms) {
                output += "  " + room + "\n";
            }
        }
        printIncoming(output);
    } else if (message.type == protocol::TYPE_ROOM_MEMBERS_RESP) {
        std::string output = "--- Members of '" + message.room + "' (page " +
                             std::to_string(message.page) + "/" +
                             std::to_string(message.totalPages) + ", total " +
                             std::to_string(message.totalMembers) + ") ---\n";
        if (message.members.empty()) {
            output += "(no members on this page)\n";
        } else {
            for (const auto& member : message.members) {
                output += "  " + member + "\n";
            }
        }
        printIncoming(output);
    } else if (message.type == protocol::TYPE_ROOM_MSG) {
        printIncoming("[" + message.room + "] " + message.from + ": " + message.content);
    } else if (message.type == protocol::TYPE_ROOM_USER_JOINED ||
               message.type == protocol::TYPE_ROOM_USER_LEFT) {
        if (!message.detail.empty()) {
            printIncoming(message.detail);
        } else {
            printIncoming("[Room] " + message.username +
                          (message.type == protocol::TYPE_ROOM_USER_JOINED ? " joined " : " left ") +
                          message.room + ".");
        }
    } else if (message.type == protocol::TYPE_HISTORY_RESP) {
        std::string label = message.conversationType;
        if (label == protocol::CONV_DIRECT && !message.withUser.empty()) {
            label += " with " + message.withUser;
        } else if (label == protocol::CONV_ROOM && !message.room.empty()) {
            label += " '" + message.room + "'";
        }
        std::string output = "--- History: " + label + " (" +
                             std::to_string(message.historyItems.size()) + " messages) ---\n";
        if (message.historyItems.empty()) {
            output += "(no messages)\n";
        } else {
            for (const auto& item : message.historyItems) {
                output += "[" + formatTimestamp(item.timestamp) + "] ";
                if (item.type == protocol::CONV_DIRECT) {
                    output += item.from + " -> " + item.to + ": ";
                } else if (item.type == protocol::CONV_ROOM) {
                    output += item.from + " @" + item.room + ": ";
                } else {
                    output += item.from + ": ";
                }
                output += item.content + " (" + item.msgId + ")\n";
            }
        }
        if (message.hasMore) {
            output += "(more messages available; use before <msg-id> to page back)\n";
        }
        printIncoming(output);
    } else if (message.type == protocol::TYPE_ERROR) {
        discardPending(message.id);
        printIncoming("[Error] " + message.code +
                      (message.detail.empty() ? "" : ": " + message.detail));
    } else if (!message.content.empty()) {
        printIncoming("[Server] " + message.content);
    } else {
        printIncoming("[Server] " + message.type);
    }
}

void ChatSession::printHelp() {
    printPlain(
        "Available commands:\n"
        "  /help                                  Show this help\n"
        "  /whoami                                Show your username\n"
        "  /status                                Show connection information\n"
        "  /users [page] [limit]                  List online users\n"
        "  /msg <user> <message>                  Send a private message (alias /dm)\n"
        "  /create <room>                         Create and join a chat room\n"
        "  /join <room>                           Join an existing chat room\n"
        "  /leave <room>                          Leave a chat room\n"
        "  /rooms [page] [limit]                  List active rooms\n"
        "  /members <room> [page] [limit]         List members of a room\n"
        "  /room <room> <message>                 Send a room message (alias /rmsg)\n"
        "  /history [public] [limit] [before|after <msg-id>]\n"
        "  /history dm <user> [limit] [before|after <msg-id>]\n"
        "  /history room <room> [limit] [before|after <msg-id>]\n"
        "  /quit                                  Disconnect and exit\n"
        "\n"
        "Anything not starting with '/' is sent as a public chat message.");
}

bool ChatSession::sendStructured(const protocol::StructuredMessage& message, bool reportError) {
    std::string wire = message.serializeWire();
    if (wire.size() > protocol::MAX_MESSAGE_SIZE) {
        if (reportError) {
            printPlain("[Error] Message exceeds the " +
                       std::to_string(protocol::MAX_MESSAGE_SIZE) +
                       "-byte protocol limit.");
        }
        return false;
    }
    if (!client_.send_raw(wire)) {
        if (reportError) {
            printPlain("[Error] Failed to send message: connection lost.");
        }
        stop();
        return false;
    }
    return true;
}

void ChatSession::rememberPending(const std::string& id, EchoKind kind,
                                  const std::string& target, const std::string& content) {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    pendingEchoes_[id] = PendingEcho{kind, target, content};
}

bool ChatSession::takePending(const std::string& id, EchoKind expected, PendingEcho& out) {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    auto it = pendingEchoes_.find(id);
    if (it == pendingEchoes_.end() || it->second.kind != expected) {
        return false;
    }
    out = it->second;
    pendingEchoes_.erase(it);
    return true;
}

void ChatSession::discardPending(const std::string& id) {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    pendingEchoes_.erase(id);
}

void ChatSession::printPrompt() {
    std::lock_guard<std::mutex> lock(outputMutex_);
    if (running_ && !promptVisible_) {
        std::cout << "> " << std::flush;
        promptVisible_ = true;
    }
}

void ChatSession::printIncoming(const std::string& text) {
    std::lock_guard<std::mutex> lock(outputMutex_);
    if (promptVisible_) {
        std::cout << "\r";
        promptVisible_ = false;
    }
    std::cout << text << "\n";
    if (running_ && registered_) {
        std::cout << "> " << std::flush;
        promptVisible_ = true;
    } else {
        std::cout << std::flush;
    }
}

void ChatSession::printPlain(const std::string& text) {
    std::lock_guard<std::mutex> lock(outputMutex_);
    if (promptVisible_) {
        std::cout << "\r";
        promptVisible_ = false;
    }
    std::cout << text << "\n" << std::flush;
}

void ChatSession::notifyInputConsumed() {
    std::lock_guard<std::mutex> lock(outputMutex_);
    promptVisible_ = false;
}

std::string ChatSession::formatTimestamp(std::int64_t timestamp) {
    if (timestamp <= 0) {
        return "--";
    }
    std::time_t value = static_cast<std::time_t>(timestamp);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &value);
#else
    localtime_r(&value, &localTime);
#endif
    char buffer[32] = {0};
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime) == 0) {
        return "--";
    }
    return std::string(buffer);
}

bool ChatSession::parsePositiveInt(const std::string& text, int& value) {
    if (text.empty()) {
        return false;
    }
    try {
        std::size_t consumed = 0;
        int parsed = std::stoi(text, &consumed);
        if (consumed != text.size() || parsed <= 0) {
            return false;
        }
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

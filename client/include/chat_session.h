#ifndef CHAT_SESSION_H
#define CHAT_SESSION_H

// chat_session.h - Shared state and protocol handling for the interactive client.
// The sender thread parses local commands and the receiver thread dispatches
// structured server events through this class.

#include "client.h"
#include "protocol_message.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

class ChatSession {
public:
    enum class RegistrationOutcome {
        Success,
        UsernameTaken,
        InvalidUsername,
        ConnectionFailed,
        UnexpectedError
    };

    explicit ChatSession(Client& client);

    ChatSession(const ChatSession&) = delete;
    ChatSession& operator=(const ChatSession&) = delete;

    // Sends a structured register_req and waits for register_ack / error.
    RegistrationOutcome registerUser(const std::string& username, std::string& detail);

    // Processes one console line. Returns false when the client should exit.
    bool handleCommand(const std::string& line);

    // Processes one complete newline-delimited frame from the server.
    void handleServerLine(const std::string& line);

    void start();
    void stop();
    bool isRunning() const;
    bool isRegistered() const;

    const std::string& username() const;

    // Console rendering helpers (thread-safe).
    void printPrompt();
    void printIncoming(const std::string& text);
    void printPlain(const std::string& text);
    void notifyInputConsumed();

private:
    Client& client_;
    std::string username_;
    std::atomic<bool> running_{false};
    std::atomic<bool> registered_{false};
    std::atomic<std::uint64_t> requestCounter_{0};
    std::mutex outputMutex_;
    bool promptVisible_{false};

    std::string nextRequestId(const char* prefix);
    bool sendStructured(const protocol::StructuredMessage& message, bool reportError = true);
    void dispatchServerMessage(const protocol::StructuredMessage& message);
    void printHelp();

    static std::string formatTimestamp(std::int64_t timestamp);
    static bool parsePositiveInt(const std::string& text, int& value);
};

#endif // CHAT_SESSION_H

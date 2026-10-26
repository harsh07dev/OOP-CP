#ifndef SENDER_H
#define SENDER_H

// sender.h - Declaration of user input sender functionality.
// Reads console input without blocking indefinitely and forwards commands or
// chat messages through the shared ChatSession.

#include <atomic>

class Client;
class ChatSession;

class Sender {
public:
    Sender(Client& client, ChatSession& session);
    ~Sender();

    // Disable copy semantics
    Sender(const Sender&) = delete;
    Sender& operator=(const Sender&) = delete;

    void start();
    void stop();
    bool is_running() const;

private:
    Client& client_;
    ChatSession& session_;
    std::atomic<bool> is_running_;
};

#endif // SENDER_H

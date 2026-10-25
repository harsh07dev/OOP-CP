#ifndef RECEIVER_H
#define RECEIVER_H

// receiver.h - Declaration of message receiver functionality.
// Continuously receives structured newline-delimited frames from the server
// and dispatches them through the shared ChatSession.

#include <atomic>

class Client;
class ChatSession;

class Receiver {
public:
    Receiver(Client& client, ChatSession& session);
    ~Receiver();

    // Disable copy semantics
    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;

    void start();
    void stop();
    bool is_running() const;

private:
    Client& client_;
    ChatSession& session_;
    std::atomic<bool> is_running_;
};

#endif // RECEIVER_H

#ifndef SENDER_H
#define SENDER_H

// sender.h - Declaration of user input sender functionality.
// Reads user input from the console and forwards chat messages to the server; detects /quit command.

#include <atomic>
#include <string>

class Client;

class Sender {
public:
    explicit Sender(Client& client);
    ~Sender();

    // Disable copy semantics
    Sender(const Sender&) = delete;
    Sender& operator=(const Sender&) = delete;

    void start();
    void stop();
    bool is_running() const;

private:
    Client& client_;
    std::atomic<bool> is_running_;
};

#endif // SENDER_H

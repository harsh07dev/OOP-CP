#ifndef RECEIVER_H
#define RECEIVER_H

// receiver.h - Declaration of message receiver functionality.
// Continuously listens for incoming server messages on a background thread and displays them on the console.

#include <atomic>

class Client;

class Receiver {
public:
    explicit Receiver(Client& client);
    ~Receiver();

    // Disable copy semantics
    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;

    void start();
    void stop();
    bool is_running() const;

private:
    Client& client_;
    std::atomic<bool> is_running_;
};

#endif // RECEIVER_H

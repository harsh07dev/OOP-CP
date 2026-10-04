// receiver.cpp - Implementation of the receiver worker.
// Continuously receives incoming broadcast messages from the server socket and prints them to the console.

#include "receiver.h"
#include "client.h"

#include <iostream>

Receiver::Receiver(Client& client)
    : client_(client),
      is_running_(false) {
}

Receiver::~Receiver() {
    stop();
}

void Receiver::start() {
    is_running_ = true;
    char buffer[4096];

    while (is_running_ && client_.is_connected()) {
        int bytes_received = client_.receive_raw(buffer, sizeof(buffer) - 1);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            std::cout << buffer << std::flush;
        } else if (bytes_received == 0) {
            if (is_running_) {
                std::cout << "\n[Notice] Server closed connection. Press Enter to exit." << std::endl;
                client_.disconnect();
            }
            break;
        } else {
            // Receive error occurred
            if (is_running_ && client_.is_connected()) {
                std::cerr << "\n[Error] Connection error while receiving from server." << std::endl;
                client_.disconnect();
            }
            break;
        }
    }

    is_running_ = false;
}

void Receiver::stop() {
    is_running_ = false;
}

bool Receiver::is_running() const {
    return is_running_;
}

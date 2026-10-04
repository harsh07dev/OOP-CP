// sender.cpp - Implementation of the sender worker.
// Continuously reads console input lines, serializes messages via protocol, and sends them to the server.

#include "sender.h"
#include "client.h"

#include <iostream>

Sender::Sender(Client& client)
    : client_(client),
      is_running_(false) {
}

Sender::~Sender() {
    stop();
}

void Sender::start() {
    is_running_ = true;
    std::string line;

    while (is_running_ && client_.is_connected()) {
        std::cout << "> " << std::flush;

        if (!std::getline(std::cin, line)) {
            // EOF or console stream closed
            break;
        }

        if (!is_running_ || !client_.is_connected()) {
            break;
        }

        // Handle /quit command
        if (line == "/quit") {
            std::cout << "\n[Notice] Disconnecting from server..." << std::endl;
            client_.disconnect();
            break;
        }

        // Send non-empty message through the client socket
        if (!line.empty()) {
            if (!client_.send_message(line)) {
                std::cerr << "\n[Error] Failed to send message: connection lost." << std::endl;
                client_.disconnect();
                break;
            }
        }
    }

    is_running_ = false;
}

void Sender::stop() {
    is_running_ = false;
}

bool Sender::is_running() const {
    return is_running_;
}

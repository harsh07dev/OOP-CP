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
        if (!std::getline(std::cin, line)) {
            break;
        }

        if (line == "/quit") {
            client_.disconnect();
            break;
        }

        if (!line.empty()) {
            client_.send_raw(line + "\n");
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

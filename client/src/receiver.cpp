// receiver.cpp - Implementation of the receiver worker.
// Continuously receives incoming broadcast messages from the server socket and prints them to the console.

#include "receiver.h"
#include "client.h"
#include "protocol_handler.h"

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
    std::string line;

    while (is_running_ && client_.is_connected()) {
        if (client_.receive_line(line)) {
            // Parse incoming payload through the protocol handler
            ClientMessage msg = ProtocolHandler::parse(line);

            // Print incoming message cleanly and restore prompt
            std::cout << "\r";
            if (msg.category == MessageCategory::SYSTEM) {
                std::cout << msg.text << "\n";
            } else {
                std::cout << msg.text << "\n";
            }
            std::cout << "> " << std::flush;
        } else {
            // Connection closed or error occurred
            if (is_running_) {
                std::cout << "\n[Notice] Server disconnected. Press Enter to exit." << std::endl;
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

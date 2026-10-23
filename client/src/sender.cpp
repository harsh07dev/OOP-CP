// sender.cpp - Implementation of the sender worker.
// Continuously reads console input lines, serializes messages via protocol, and sends them to the server.

#include "sender.h"
#include "client.h"
#include "protocol_handler.h"
#include "protocol.h"

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
        if (line == protocol::CMD_QUIT) {
            std::cout << "\n[Notice] Disconnecting from server..." << std::endl;
            client_.send_raw(ProtocolHandler::format_leave());
            client_.disconnect();
            break;
        }

        // Reject empty messages
        if (line.empty()) {
            continue;
        }

        // Validate message size before sending (wire size including newline must not exceed MAX_MESSAGE_SIZE)
        if (line.size() + 1 > protocol::MAX_MESSAGE_SIZE) {
            std::cout << "[Warning] Message too long (" << (line.size() + 1)
                      << " bytes). Max allowed is " << protocol::MAX_MESSAGE_SIZE
                      << " bytes. Message was not sent." << std::endl;
            continue;
        }

        std::string payload = ProtocolHandler::format_chat(line);
        if (!client_.send_raw(payload)) {
            std::cerr << "\n[Error] Failed to send message: connection lost." << std::endl;
            client_.disconnect();
            break;
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

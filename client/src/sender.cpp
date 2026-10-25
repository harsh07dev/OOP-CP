// sender.cpp - Implementation of the sender worker.
// Waits for console input with a short timeout so server disconnects can stop
// the client promptly, then delegates command handling to ChatSession.

#include "sender.h"
#include "chat_session.h"
#include "client.h"
#include "console_input.h"

#include <iostream>
#include <string>

Sender::Sender(Client& client, ChatSession& session)
    : client_(client),
      session_(session),
      is_running_(false) {
}

Sender::~Sender() {
    stop();
}

void Sender::start() {
    is_running_ = true;

    while (is_running_ && session_.isRunning() && client_.is_connected()) {
        session_.printPrompt();

        // Wait for a line without blocking forever.  This lets the sender
        // notice a server-side disconnect even while the user is idle.
        while (is_running_ && session_.isRunning() && client_.is_connected()) {
            if (!console_input::is_readable(200)) {
                continue;
            }

            std::string line;
            if (!std::getline(std::cin, line)) {
                session_.stop();
                is_running_ = false;
                return;
            }

            session_.notifyInputConsumed();
            if (!session_.handleCommand(line)) {
                is_running_ = false;
                return;
            }
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

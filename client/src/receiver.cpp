// receiver.cpp - Implementation of the receiver worker.
// Reads complete newline-delimited frames and asks ChatSession to render them.

#include "receiver.h"
#include "chat_session.h"
#include "client.h"

#include <iostream>
#include <string>

Receiver::Receiver(Client& client, ChatSession& session)
    : client_(client),
      session_(session) {
}

Receiver::~Receiver() {
    stop();
}

void Receiver::start() {
    is_running_ = true;
    std::string line;

    while (is_running_ && session_.isRunning() && client_.is_connected()) {
        if (client_.receive_line(line)) {
            session_.handleServerLine(line);
        } else {
            if (is_running_ && session_.isRunning()) {
                session_.stop();
                session_.printIncoming("[Notice] Server disconnected.");
            } else {
                session_.stop();
            }
            client_.disconnect();
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

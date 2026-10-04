// main.cpp - Entry point for the chat server application.
// Starts listening on port 8080, accepts client connections, and receives incoming messages.

#include "server.h"

#include <iostream>
#include <csignal>

namespace {
    Server* g_serverInstance = nullptr;

    void handleSignal(int signum) {
        if (signum == SIGINT || signum == SIGTERM) {
            if (g_serverInstance != nullptr) {
                g_serverInstance->stop();
            }
        }
    }
}

int main() {
    // Register signal handlers for clean termination on SIGINT (Ctrl+C) and SIGTERM
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    constexpr int SERVER_PORT = 8080;

    // 1. Create Server instance on port 8080
    Server server(SERVER_PORT);
    g_serverInstance = &server;

    // 2. Start server sequence: socket() -> bind() -> listen()
    if (!server.start()) {
        std::cerr << "[FATAL] Server startup sequence failed. Exiting." << std::endl;
        return 1;
    }

    // 3. Process client connections and incoming messages sequentially
    while (server.isRunning()) {
        // Accept incoming client connection (blocking call)
        if (!server.acceptClient()) {
            break;
        }

        // Receive message(s) from the connected client until disconnection
        while (server.hasClientConnected() && server.isRunning()) {
            if (!server.receiveMessage()) {
                break; // Client disconnected or error occurred
            }
        }
    }

    return 0;
}

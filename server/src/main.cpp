// main.cpp - Entry point for the chat server application.
// Initializes the Server instance on port 8080, starts listening, and keeps the process alive.

#include "server.h"

#include <iostream>
#include <thread>
#include <chrono>
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
    // Register signal handlers for graceful shutdown on SIGINT (Ctrl+C) and SIGTERM
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    constexpr int SERVER_PORT = 8080;

    // 1. Create a Server object using port 8080
    Server server(SERVER_PORT);
    g_serverInstance = &server;

    // 2. Start the server startup sequence
    // 3. Report startup errors clearly
    if (!server.start()) {
        std::cerr << "[FATAL] Server startup sequence failed. Exiting.\n";
        return 1;
    }

    // 4. Keep the process alive after successful listen() for this prototype
    while (server.isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    return 0;
}

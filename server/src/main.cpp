// main.cpp - Entry point for the chat server application.
// Starts listening on port 8080 and continuously accepts incoming client connections,
// spawning a dedicated worker thread per client.

#include "server.h"
#include "net_compat.h"

#include <iostream>
#include <csignal>
#include <atomic>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

namespace {
    std::atomic<bool> g_stopRequested{false};
    Server* g_serverInstance = nullptr;

    void handleSignal(int signum) {
        (void)signum;
        g_stopRequested.store(true);
#ifdef _WIN32
        if (g_serverInstance != nullptr) {
            g_serverInstance->stop();
        }
#else
        // On Linux / POSIX systems, signal handlers must remain strictly async-signal-safe.
        // Complex operations (mutex locking, thread joining, heap allocations, and stream I/O)
        // are avoided here. The main accept() loop is interrupted with EINTR (configured via
        // sigaction without SA_RESTART), allowing graceful coordinated shutdown on the main thread.
#endif
    }

#ifdef _WIN32
    BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
        if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT || ctrlType == CTRL_CLOSE_EVENT) {
            g_stopRequested.store(true);
            if (g_serverInstance != nullptr) {
                g_serverInstance->stop();
            }
            return TRUE;
        }
        return FALSE;
    }
#endif
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // Register Windows console control and signal handlers
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);
#else
    // Configure POSIX signal handling with sigaction.
    // Explicitly omit SA_RESTART so blocking network syscalls (e.g. accept) unblock with EINTR.
    struct sigaction sa{};
    sa.sa_handler = handleSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    std::signal(SIGPIPE, SIG_IGN); // Ignore SIGPIPE so broken client sockets do not terminate server
#endif

    int serverPort = 8080;
    std::string dbPath = "chat_history.db";

    if (argc > 1) {
        try {
            serverPort = std::stoi(argv[1]);
        } catch (...) {
            serverPort = 8080;
        }
    }
    if (argc > 2) {
        dbPath = argv[2];
    }

    // 1. Create Server instance on configured port and database path
    Server server(serverPort, dbPath);
    g_serverInstance = &server;

    // 2. Start server sequence: socket() -> bind() -> listen()
    if (!server.start()) {
        std::cerr << "[FATAL] Server startup sequence failed. Exiting." << std::endl;
        return 1;
    }

    // 3. Continuously accept clients on the main thread.
    // Each accepted client is dispatched to an independent worker thread inside acceptClient(),
    // allowing multiple clients to remain connected simultaneously.
    while (server.isRunning() && !g_stopRequested.load()) {
        if (!server.acceptClient()) {
            break;
        }
    }

    // Coordinated teardown on main thread (safe across both platforms)
    server.stop();
    g_serverInstance = nullptr;

    return 0;
}

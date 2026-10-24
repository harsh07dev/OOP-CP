// server.h - Declaration of the Server class for cross-platform TCP chat.
// Manages server socket setup, binding, listening, and accepting client connections concurrently with worker threads.

#pragma once

#include "net_compat.h"
#include <thread>
#include <string>
#include <atomic>
#include <vector>
#include <memory>
#include <mutex>

#include "client_manager.h"
#include "room_manager.h"
#include "logger.h"

struct WorkerRecord {
    std::thread th;
    std::shared_ptr<std::atomic<bool>> done;
    SOCKET socket{INVALID_SOCKET};
    std::shared_ptr<std::atomic<bool>> isClosed;
};

class Server {
public:
    explicit Server(int port = 8080);
    ~Server();

    // Disable copy semantics to prevent duplicate socket ownership issues
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Executes the complete TCP startup sequence: create -> configure -> bind -> listen
    bool start();

    // Accepts an incoming client connection and dispatches a dedicated worker thread
    bool acceptClient();

    // Shuts down the server and closes the listening socket
    void stop();

    // Checks if the server is currently in an active listening state
    bool isRunning() const;

    // Returns the port the server is configured to listen on
    int getPort() const;

private:
    // Core TCP sequence steps
    bool initializeSocket();
    bool bindSocket();
    bool startListening();
    void cleanup();

    // Helper to safely close and invalidate a socket handle
    void closeSocket(SOCKET& s);

    // Dedicated worker thread handler per connected client
    void handleClient(SOCKET clientSocket, sockaddr_in clientAddr, std::shared_ptr<std::atomic<bool>> isClosed);

    // State required for server networking and client management
    int m_port;
    SOCKET m_serverSocket;    // Listening socket: listens for incoming TCP connections
    sockaddr_in m_serverAddr; // Server address configuration
    Logger m_logger;          // Thread-safe timestamped logger
    ClientManager m_clientManager; // Thread-safe active client registry
    RoomManager m_roomManager;     // Thread-safe room registry and membership
    std::atomic<bool> m_isRunning;
    std::atomic<bool> m_netInitialized;

    // Thread tracking to ensure coordinated shutdown and zero detached threads
    std::mutex m_threadsMutex;
    std::vector<WorkerRecord> m_workers;

    // Monotonic message sequence counter for structured message identification
    std::atomic<uint64_t> m_messageSequence{0};
};

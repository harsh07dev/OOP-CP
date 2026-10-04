// server.h - Declaration of the Server class for Windows Winsock.
// Manages server socket setup, binding, listening, and accepting client connections concurrently with worker threads.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <string>

#include "client_manager.h"
#include "logger.h"

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
    void handleClient(SOCKET clientSocket, sockaddr_in clientAddr);

    // State required for server networking and client management
    int m_port;
    SOCKET m_serverSocket;    // Listening socket: listens for incoming TCP connections
    sockaddr_in m_serverAddr; // Server address configuration
    Logger m_logger;          // Thread-safe timestamped logger
    ClientManager m_clientManager; // Thread-safe active client registry
    bool m_isRunning;
    bool m_wsaInitialized;
};

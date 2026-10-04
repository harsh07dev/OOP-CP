// server.h - Declaration of the Server class for Windows Winsock.
// Manages server socket setup, binding, listening, and accepting client connections.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

class Server {
public:
    explicit Server(int port = 8080);
    ~Server();

    // Disable copy semantics to prevent duplicate socket ownership issues
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Executes the complete TCP startup sequence: create -> configure -> bind -> listen
    bool start();

    // Accepts an incoming client connection on the listening socket
    bool acceptClient();

    // Receives a text message from the currently connected client
    bool receiveMessage();

    // Shuts down the server and closes all associated socket resources
    void stop();

    // Checks if the server is currently in an active listening state
    bool isRunning() const;

    // Returns the port the server is configured to listen on
    int getPort() const;

    // Returns the accepted client socket descriptor
    SOCKET getClientSocket() const;

    // Checks if a client is currently connected
    bool hasClientConnected() const;

private:
    // Core TCP sequence steps
    bool initializeSocket();
    bool bindSocket();
    bool startListening();
    void cleanup();

    // Helper to safely close and invalidate a socket handle
    void closeSocket(SOCKET& s);

    // State required for server networking and client connection
    int m_port;
    SOCKET m_serverSocket;    // Listening socket: listens for incoming TCP connections
    SOCKET m_clientSocket;    // Client socket: communicates with the accepted client
    sockaddr_in m_serverAddr; // Server address configuration
    sockaddr_in m_clientAddr; // Accepted client address information
    bool m_isRunning;
    bool m_clientConnected;
    bool m_wsaInitialized;
};

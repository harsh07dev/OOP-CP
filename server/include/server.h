// server.h - Declaration of the Server class.
// Manages server socket setup, address configuration, binding, and listening for Phase 1 prototype.

#pragma once

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socket_t = SOCKET;
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    using socket_t = int;
    #ifndef INVALID_SOCKET
        #define INVALID_SOCKET (-1)
    #endif
    #ifndef SOCKET_ERROR
        #define SOCKET_ERROR (-1)
    #endif
#endif

class Server {
public:
    explicit Server(int port = 8080);
    ~Server();

    // Disable copy semantics to prevent duplicate socket ownership issues
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Executes the complete TCP startup sequence: create -> configure -> bind -> listen
    bool start();

    // Shuts down the server and closes all associated socket resources
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

    // Cross-platform helper to safely close a socket handle
    void closeSocket(socket_t s);

    // Minimal state required for Phase 1
    int m_port;
    socket_t m_serverSocket;
    sockaddr_in m_serverAddr;
    bool m_isRunning;

#ifdef _WIN32
    bool m_wsaInitialized;
#endif
};

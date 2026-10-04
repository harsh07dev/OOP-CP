// server.cpp - Implementation of the Server class.
// Handles TCP socket creation, address configuration, binding, and listening.

#include "server.h"

#include <iostream>
#include <cstring>
#include <cstdint>
#include <cerrno>

namespace {
    // Helper function to retrieve the last platform-specific socket error code
    int getLastSocketError() {
#ifdef _WIN32
        return WSAGetLastError();
#else
        return errno;
#endif
    }
}

Server::Server(int port)
    : m_port(port),
      m_serverSocket(INVALID_SOCKET),
      m_isRunning(false)
#ifdef _WIN32
      , m_wsaInitialized(false)
#endif
{
    std::memset(&m_serverAddr, 0, sizeof(m_serverAddr));
}

Server::~Server() {
    cleanup();
}

bool Server::start() {
    std::cout << "================================\n";
    std::cout << "        TCP CHAT SERVER         \n";
    std::cout << "================================\n" << std::endl;

    // Step 1: Create socket and configure address structure
    if (!initializeSocket()) {
        std::cerr << "[ERROR] Failed to initialize server socket." << std::endl;
        cleanup();
        return false;
    }
    std::cout << "Server socket created." << std::endl;

    // Step 2: Bind socket to IP address and port
    if (!bindSocket()) {
        std::cerr << "[ERROR] Failed to bind socket to port " << m_port << "." << std::endl;
        cleanup();
        return false;
    }
    std::cout << "Server bound to port " << m_port << "." << std::endl;

    // Step 3: Put socket into listening state
    if (!startListening()) {
        std::cerr << "[ERROR] Failed to start listening on port " << m_port << "." << std::endl;
        cleanup();
        return false;
    }
    std::cout << "Server listening on port " << m_port << "...\n" << std::endl;
    std::cout << "Waiting for clients..." << std::endl;

    m_isRunning = true;
    return true;
}

bool Server::initializeSocket() {
#ifdef _WIN32
    // Initialize Winsock on Windows platforms
    WSADATA wsaData;
    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaResult != 0) {
        std::cerr << "[ERROR] WSAStartup failed with error code: " << wsaResult << "\n";
        return false;
    }
    m_wsaInitialized = true;
#endif

    // Create a TCP stream socket using IPv4
    // AF_INET     = IPv4 Internet protocols
    // SOCK_STREAM = Sequenced, reliable, two-way, connection-based byte streams (TCP)
    // 0           = Default protocol (IPPROTO_TCP)
    m_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_serverSocket == INVALID_SOCKET) {
        std::cerr << "[ERROR] socket() creation failed with error code: " << getLastSocketError() << "\n";
        return false;
    }

    // Set SO_REUSEADDR so the port can be rebound immediately upon restart
    int opt = 1;
#ifdef _WIN32
    if (setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt)) == SOCKET_ERROR) {
        std::cerr << "[WARNING] setsockopt(SO_REUSEADDR) failed with error: " << getLastSocketError() << "\n";
    }
#else
    if (setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "[WARNING] setsockopt(SO_REUSEADDR) failed with error: " << getLastSocketError() << "\n";
    }
#endif

    // Configure the server address structure
    // INADDR_ANY (0.0.0.0) binds the server to listen on all available local network interfaces.
    // Notice: 0.0.0.0 is the server's listening interface, whereas clients connect via the host's actual LAN IP.
    std::memset(&m_serverAddr, 0, sizeof(m_serverAddr));
    m_serverAddr.sin_family = AF_INET;
    m_serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    m_serverAddr.sin_port = htons(static_cast<uint16_t>(m_port));

    return true;
}

bool Server::bindSocket() {
    // Bind the socket to the IP address and port specified in m_serverAddr
    int result = bind(m_serverSocket, reinterpret_cast<struct sockaddr*>(&m_serverAddr), sizeof(m_serverAddr));
    if (result == SOCKET_ERROR) {
        int err = getLastSocketError();
        std::cerr << "[ERROR] bind() failed with error code: " << err << "\n";

#ifdef _WIN32
        if (err == WSAEADDRINUSE) {
            std::cerr << "[ERROR] Diagnosis: Port " << m_port << " is already in use by another application.\n";
        } else if (err == WSAEACCES) {
            std::cerr << "[ERROR] Diagnosis: Access denied. Insufficient permissions to bind to port " << m_port << ".\n";
        } else if (err == WSAENOTSOCK) {
            std::cerr << "[ERROR] Diagnosis: Invalid socket descriptor specified.\n";
        } else {
            std::cerr << "[ERROR] Diagnosis: Check socket state or network configuration.\n";
        }
#else
        if (err == EADDRINUSE) {
            std::cerr << "[ERROR] Diagnosis: Port " << m_port << " is already in use by another application.\n";
        } else if (err == EACCES) {
            std::cerr << "[ERROR] Diagnosis: Access denied. Insufficient permissions to bind to port " << m_port << ".\n";
        } else if (err == ENOTSOCK) {
            std::cerr << "[ERROR] Diagnosis: Invalid socket descriptor specified.\n";
        } else {
            std::cerr << "[ERROR] Diagnosis: Check socket state or network configuration (" << strerror(err) << ").\n";
        }
#endif
        return false;
    }
    return true;
}

bool Server::startListening() {
    // Put socket into listening state
    // Backlog specifies the maximum length of the queue of pending connections (5)
    constexpr int BACKLOG = 5;
    int result = listen(m_serverSocket, BACKLOG);
    if (result == SOCKET_ERROR) {
        std::cerr << "[ERROR] listen() failed with error code: " << getLastSocketError() << "\n";
        return false;
    }
    return true;
}

void Server::closeSocket(socket_t s) {
    if (s != INVALID_SOCKET) {
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
    }
}

void Server::cleanup() {
    if (m_serverSocket != INVALID_SOCKET) {
        closeSocket(m_serverSocket);
        m_serverSocket = INVALID_SOCKET;
    }

#ifdef _WIN32
    if (m_wsaInitialized) {
        WSACleanup();
        m_wsaInitialized = false;
    }
#endif

    m_isRunning = false;
}

void Server::stop() {
    if (m_isRunning) {
        std::cout << "\nStopping server...\n";
        cleanup();
        std::cout << "Server stopped.\n";
    }
}

bool Server::isRunning() const {
    return m_isRunning;
}

int Server::getPort() const {
    return m_port;
}

// server.cpp - Implementation of the Server class for Windows Winsock.
// Handles TCP socket creation, binding, listening, and accepting client connections.

#include "server.h"

#include <iostream>
#include <cstring>
#include <cstdint>

Server::Server(int port)
    : m_port(port),
      m_serverSocket(INVALID_SOCKET),
      m_clientSocket(INVALID_SOCKET),
      m_isRunning(false),
      m_clientConnected(false),
      m_wsaInitialized(false)
{
    std::memset(&m_serverAddr, 0, sizeof(m_serverAddr));
    std::memset(&m_clientAddr, 0, sizeof(m_clientAddr));
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

    m_isRunning = true;
    return true;
}

bool Server::acceptClient() {
    if (!m_isRunning || m_serverSocket == INVALID_SOCKET) {
        std::cerr << "[ERROR] Server is not listening. Cannot accept connections." << std::endl;
        return false;
    }

    std::cout << "Waiting for client connection..." << std::endl;

    // accept() is a blocking call: it waits until an incoming client connection is received.
    // The listening socket (m_serverSocket) accepts the connection and returns a new
    // dedicated client socket (m_clientSocket) for subsequent communication.
    int clientAddrLen = sizeof(m_clientAddr);
    std::memset(&m_clientAddr, 0, sizeof(m_clientAddr));

    m_clientSocket = accept(m_serverSocket, reinterpret_cast<SOCKADDR*>(&m_clientAddr), &clientAddrLen);
    if (m_clientSocket == INVALID_SOCKET) {
        int err = WSAGetLastError();
        // If the server was deliberately stopped (e.g., via signal handler closing the socket), exit cleanly
        if (!m_isRunning || err == WSAEINTR || err == WSAENOTSOCK) {
            return false;
        }
        std::cerr << "[ERROR] accept() failed with error code: " << err << std::endl;
        return false;
    }

    // Extract human-readable IP address from the client sockaddr_in structure
    char ipBuffer[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &(m_clientAddr.sin_addr), ipBuffer, sizeof(ipBuffer)) == nullptr) {
        std::strncpy(ipBuffer, "Unknown", sizeof(ipBuffer) - 1);
    }

    // Extract client port (converted from network byte order to host byte order)
    int clientPort = ntohs(m_clientAddr.sin_port);

    std::cout << "\nClient connected!" << std::endl;
    std::cout << "Client IP: " << ipBuffer << std::endl;
    std::cout << "Client Port: " << clientPort << std::endl;

    m_clientConnected = true;
    return true;
}

bool Server::receiveMessage() {
    if (!m_clientConnected || m_clientSocket == INVALID_SOCKET) {
        std::cerr << "[ERROR] No client connected to receive messages from." << std::endl;
        return false;
    }

    std::cout << "\nWaiting for client message..." << std::endl;

    // Buffer to store incoming data.
    // Fixed-size buffer of 1024 bytes for this prototype.
    // Passing (BUFFER_SIZE - 1) guarantees space for a null terminator ('\0') without buffer overflow.
    constexpr size_t BUFFER_SIZE = 1024;
    char buffer[BUFFER_SIZE] = {0};

    // Note on TCP Byte Streams:
    // TCP is a connection-oriented, reliable byte-stream protocol.
    // Unlike datagram-based protocols (e.g., UDP), TCP does not preserve application-level
    // message boundaries. Data is transmitted as a continuous sequence of bytes.
    // Therefore, one send() call on the client does not necessarily correspond to an exact
    // single recv() call on the server in production (data can coalesce or fragment).
    // For this prototype, plain text messages fit within the buffer and are processed directly.
    // Explicit message framing and delimiters will be introduced in subsequent phases.
    int bytesReceived = recv(m_clientSocket, buffer, static_cast<int>(BUFFER_SIZE - 1), 0);

    // Case 1: Data was successfully received (bytesReceived > 0)
    if (bytesReceived > 0) {
        // Safe null-termination: ensure buffer is a valid C-string
        buffer[bytesReceived] = '\0';

        // Trim any trailing carriage return / newline characters for clean console display
        while (bytesReceived > 0 && (buffer[bytesReceived - 1] == '\n' || buffer[bytesReceived - 1] == '\r')) {
            buffer[--bytesReceived] = '\0';
        }

        std::cout << "Received: " << buffer << std::endl;
        return true;
    }
    // Case 2: Client gracefully disconnected (bytesReceived == 0)
    else if (bytesReceived == 0) {
        std::cout << "Client disconnected." << std::endl;
        closeSocket(m_clientSocket);
        m_clientConnected = false;
        return false;
    }
    // Case 3: A receive error occurred (bytesReceived < 0 / SOCKET_ERROR)
    else {
        int err = WSAGetLastError();
        // If the socket was intentionally closed during server shutdown, avoid printing an error
        if (m_isRunning && err != WSAEINTR && err != WSAENOTSOCK) {
            std::cerr << "[ERROR] recv() failed with error code: " << err << std::endl;
        }
        closeSocket(m_clientSocket);
        m_clientConnected = false;
        return false;
    }
}

bool Server::initializeSocket() {
    // Initialize Windows Winsock 2.2
    WSADATA wsaData;
    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaResult != 0) {
        std::cerr << "[ERROR] WSAStartup failed with error code: " << wsaResult << std::endl;
        return false;
    }
    m_wsaInitialized = true;

    // Create IPv4 TCP stream socket
    m_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_serverSocket == INVALID_SOCKET) {
        std::cerr << "[ERROR] socket() creation failed with error code: " << WSAGetLastError() << std::endl;
        return false;
    }

    // Set SO_REUSEADDR to enable immediate reuse of the local address/port upon restart
    int opt = 1;
    if (setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt)) == SOCKET_ERROR) {
        std::cerr << "[WARNING] setsockopt(SO_REUSEADDR) failed with error: " << WSAGetLastError() << std::endl;
    }

    // Configure the server address structure to listen on all local network interfaces (0.0.0.0)
    std::memset(&m_serverAddr, 0, sizeof(m_serverAddr));
    m_serverAddr.sin_family = AF_INET;
    m_serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    m_serverAddr.sin_port = htons(static_cast<uint16_t>(m_port));

    return true;
}

bool Server::bindSocket() {
    // Bind the listening socket to port 8080 on 0.0.0.0
    int result = bind(m_serverSocket, reinterpret_cast<SOCKADDR*>(&m_serverAddr), sizeof(m_serverAddr));
    if (result == SOCKET_ERROR) {
        int err = WSAGetLastError();
        std::cerr << "[ERROR] bind() failed with error code: " << err << std::endl;

        if (err == WSAEADDRINUSE) {
            std::cerr << "[ERROR] Diagnosis: Port " << m_port << " is already in use by another application." << std::endl;
        } else if (err == WSAEACCES) {
            std::cerr << "[ERROR] Diagnosis: Access denied. Insufficient permissions to bind to port " << m_port << "." << std::endl;
        } else if (err == WSAENOTSOCK) {
            std::cerr << "[ERROR] Diagnosis: Invalid socket descriptor specified." << std::endl;
        } else {
            std::cerr << "[ERROR] Diagnosis: Check socket state or network configuration." << std::endl;
        }
        return false;
    }
    return true;
}

bool Server::startListening() {
    // Put socket into listening mode with a connection backlog of 5
    constexpr int BACKLOG = 5;
    int result = listen(m_serverSocket, BACKLOG);
    if (result == SOCKET_ERROR) {
        std::cerr << "[ERROR] listen() failed with error code: " << WSAGetLastError() << std::endl;
        return false;
    }
    return true;
}

void Server::closeSocket(SOCKET& s) {
    if (s != INVALID_SOCKET) {
        closesocket(s);
        s = INVALID_SOCKET;
    }
}

void Server::cleanup() {
    // Close the dedicated client socket if currently open
    closeSocket(m_clientSocket);

    // Close the primary listening server socket if currently open
    closeSocket(m_serverSocket);

    // Clean up Winsock subsystem
    if (m_wsaInitialized) {
        WSACleanup();
        m_wsaInitialized = false;
    }

    m_isRunning = false;
    m_clientConnected = false;
}

void Server::stop() {
    if (m_isRunning) {
        std::cout << "\nStopping server..." << std::endl;
        cleanup();
        std::cout << "Server stopped." << std::endl;
    }
}

bool Server::isRunning() const {
    return m_isRunning;
}

int Server::getPort() const {
    return m_port;
}

SOCKET Server::getClientSocket() const {
    return m_clientSocket;
}

bool Server::hasClientConnected() const {
    return m_clientConnected;
}

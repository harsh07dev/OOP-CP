// server.cpp - Implementation of the Server class for Windows Winsock.
// Handles TCP socket creation, binding, listening, and concurrent client handling with dedicated worker threads.

#include "server.h"

#include <iostream>
#include <cstring>
#include <cstdint>

Server::Server(int port)
    : m_port(port),
      m_serverSocket(INVALID_SOCKET),
      m_isRunning(false),
      m_wsaInitialized(false)
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

    m_isRunning = true;
    return true;
}

bool Server::acceptClient() {
    if (!m_isRunning || m_serverSocket == INVALID_SOCKET) {
        std::cerr << "[ERROR] Server is not listening. Cannot accept connections." << std::endl;
        return false;
    }

    std::cout << "Waiting for client connection..." << std::endl;

    sockaddr_in clientAddr;
    int clientAddrLen = sizeof(clientAddr);
    std::memset(&clientAddr, 0, sizeof(clientAddr));

    // accept() blocks until an incoming connection arrives on the listening socket
    SOCKET clientSocket = accept(m_serverSocket, reinterpret_cast<SOCKADDR*>(&clientAddr), &clientAddrLen);
    if (clientSocket == INVALID_SOCKET) {
        int err = WSAGetLastError();
        // If the server was stopped (e.g., via signal handler closing m_serverSocket), exit cleanly
        if (!m_isRunning || err == WSAEINTR || err == WSAENOTSOCK) {
            return false;
        }
        std::cerr << "[ERROR] accept() failed with error code: " << err << std::endl;
        return false;
    }

    // Extract client IP and port for reporting
    char ipBuffer[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &(clientAddr.sin_addr), ipBuffer, sizeof(ipBuffer)) == nullptr) {
        std::strncpy(ipBuffer, "Unknown", sizeof(ipBuffer) - 1);
    }
    int clientPort = ntohs(clientAddr.sin_port);

    std::cout << "\nClient connected!" << std::endl;
    std::cout << "Client IP: " << ipBuffer << std::endl;
    std::cout << "Client Port: " << clientPort << std::endl;

    // Spawn a dedicated worker thread for this accepted client connection.
    // Detach the thread so that it runs independently, allowing the main thread
    // to immediately return to accept() and handle additional incoming clients concurrently.
    std::thread clientThread(&Server::handleClient, this, clientSocket, clientAddr);
    clientThread.detach();

    return true;
}

void Server::handleClient(SOCKET clientSocket, sockaddr_in clientAddr) {
    char ipBuffer[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &(clientAddr.sin_addr), ipBuffer, sizeof(ipBuffer)) == nullptr) {
        std::strncpy(ipBuffer, "Unknown", sizeof(ipBuffer) - 1);
    }
    int clientPort = ntohs(clientAddr.sin_port);

    std::string receiveBuffer;
    bool isRegistered = false;
    std::string registeredUsername;

    constexpr size_t RAW_BUFFER_SIZE = 1024;
    char rawBuffer[RAW_BUFFER_SIZE];

    // Continuously read network stream from this client socket
    while (m_isRunning) {
        int bytesReceived = recv(clientSocket, rawBuffer, sizeof(rawBuffer), 0);

        if (bytesReceived > 0) {
            receiveBuffer.append(rawBuffer, bytesReceived);

            // Extract and process all complete newline-delimited messages from the stream
            size_t newlinePos;
            bool shouldExit = false;

            while ((newlinePos = receiveBuffer.find('\n')) != std::string::npos) {
                std::string line = receiveBuffer.substr(0, newlinePos);
                receiveBuffer.erase(0, newlinePos + 1);

                // Strip trailing carriage return if present
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                // First message must be interpreted as username registration
                if (!isRegistered) {
                    if (line.empty()) {
                        std::cerr << "[ClientManager] Registration rejected: empty username from " 
                                  << ipBuffer << ":" << clientPort << std::endl;
                        shouldExit = true;
                        break;
                    }

                    registeredUsername = line;
                    if (m_clientManager.addClient(clientSocket, registeredUsername, clientAddr)) {
                        isRegistered = true;
                    } else {
                        std::cerr << "[ClientManager] Failed to register client: " 
                                  << registeredUsername << std::endl;
                        shouldExit = true;
                        break;
                    }
                    continue; // First message handled; proceed to next line or next recv
                }

                // Handle graceful disconnect command (/quit)
                if (line == "/quit") {
                    std::cout << "[" << registeredUsername << "] /quit" << std::endl;
                    shouldExit = true;
                    break;
                }

                // Display normal chat message on server console
                std::cout << "[" << registeredUsername << "] " << line << std::endl;
            }

            if (shouldExit) {
                break;
            }
        }
        else if (bytesReceived == 0) {
            // Client closed connection gracefully
            break;
        }
        else {
            int err = WSAGetLastError();
            if (m_isRunning && err != WSAEINTR && err != WSAECONNRESET && err != WSAENOTSOCK) {
                std::cerr << "[" << (isRegistered ? registeredUsername : (std::string(ipBuffer) + ":" + std::to_string(clientPort)))
                          << "] recv() error: " << err << std::endl;
            }
            break;
        }
    }

    // Unregister client from ClientManager if registration had succeeded
    if (isRegistered) {
        m_clientManager.removeClient(clientSocket);
        isRegistered = false;
    }

    // Cleanly close this worker's client socket exactly once
    closesocket(clientSocket);
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
    // Close the primary listening server socket if currently open
    closeSocket(m_serverSocket);

    // Clean up Winsock subsystem
    if (m_wsaInitialized) {
        WSACleanup();
        m_wsaInitialized = false;
    }

    m_isRunning = false;
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

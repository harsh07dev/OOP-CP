// server.cpp - Implementation of the Server class for cross-platform TCP chat.
// Handles TCP socket creation, binding, listening, and concurrent client handling with dedicated worker threads.

#include "server.h"
#include "protocol.h"
#include "protocol_message.h"
#include "net_compat.h"

#include <iostream>
#include <cstring>
#include <cstdint>
#include <chrono>

Server::Server(int port)
    : m_port(port),
      m_serverSocket(INVALID_SOCKET),
      m_logger("logs/chat.log"),
      m_clientManager(&m_logger),
      m_isRunning(false),
      m_netInitialized(false)
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

    m_logger.info("Server starting.");

    // Step 1: Create socket and configure address structure
    if (!initializeSocket()) {
        m_logger.error("Failed to initialize server socket.");
        cleanup();
        return false;
    }
    m_logger.info("Server socket created.");

    // Step 2: Bind socket to IP address and port
    if (!bindSocket()) {
        m_logger.error("Failed to bind socket to port " + std::to_string(m_port) + ".");
        cleanup();
        return false;
    }
    m_logger.info("Server bound to port " + std::to_string(m_port) + ".");

    // Step 3: Put socket into listening state
    if (!startListening()) {
        m_logger.error("Failed to start listening on port " + std::to_string(m_port) + ".");
        cleanup();
        return false;
    }
    m_logger.info("Server listening on port " + std::to_string(m_port) + ".");

    m_isRunning = true;
    return true;
}

bool Server::acceptClient() {
    if (!m_isRunning || m_serverSocket == INVALID_SOCKET) {
        m_logger.error("Server is not listening. Cannot accept connections.");
        return false;
    }

    std::cout << "Waiting for client connection..." << std::endl;

    sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    std::memset(&clientAddr, 0, sizeof(clientAddr));

    // accept() blocks until an incoming connection arrives on the listening socket
    SOCKET clientSocket = accept(m_serverSocket, reinterpret_cast<SOCKADDR*>(&clientAddr), &clientAddrLen);
    if (clientSocket == INVALID_SOCKET) {
        int err = netGetLastError();
        // If the server was stopped (e.g., via signal handler closing m_serverSocket), exit cleanly
        if (!m_isRunning || netIsInterrupted(err) || netIsBadSocket(err)) {
            return false;
        }
        if (netIsConnAbortedOrReset(err)) {
            return true; // Transient client disconnect before accept completed; keep accepting
        }
        m_logger.error("accept() failed with error code: " + std::to_string(err));
        return true;
    }

    // Extract client IP and port for reporting
    char ipBuffer[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &(clientAddr.sin_addr), ipBuffer, sizeof(ipBuffer)) == nullptr) {
        std::strncpy(ipBuffer, "Unknown", sizeof(ipBuffer) - 1);
    }
    int clientPort = ntohs(clientAddr.sin_port);
    std::string clientEndpoint = std::string(ipBuffer) + ":" + std::to_string(clientPort);

    m_logger.info("Client connected: " + clientEndpoint);

    // Spawn and track a dedicated worker thread for this accepted client connection.
    auto doneFlag = std::make_shared<std::atomic<bool>>(false);
    auto isClosed = std::make_shared<std::atomic<bool>>(false);
    std::thread clientThread([this, clientSocket, clientAddr, doneFlag, isClosed]() {
        handleClient(clientSocket, clientAddr, isClosed);
        doneFlag->store(true);
    });

    {
        std::lock_guard<std::mutex> lock(m_threadsMutex);
        // Clean up previously completed worker records
        for (auto it = m_workers.begin(); it != m_workers.end(); ) {
            if (it->done->load() && it->th.joinable()) {
                it->th.join();
                it = m_workers.erase(it);
            } else {
                ++it;
            }
        }
        m_workers.push_back({std::move(clientThread), doneFlag, clientSocket, isClosed});
    }

    return true;
}

void Server::handleClient(SOCKET clientSocket, sockaddr_in clientAddr, std::shared_ptr<std::atomic<bool>> isClosed) {
    char ipBuffer[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &(clientAddr.sin_addr), ipBuffer, sizeof(ipBuffer)) == nullptr) {
        std::strncpy(ipBuffer, "Unknown", sizeof(ipBuffer) - 1);
    }
    int clientPort = ntohs(clientAddr.sin_port);
    std::string clientEndpoint = std::string(ipBuffer) + ":" + std::to_string(clientPort);

    std::string receiveBuffer;
    bool isRegistered = false;
    std::string registeredUsername;
    bool userRequestedQuit = false;
    protocol::ProtocolVersion clientProto = protocol::ProtocolVersion::LEGACY_V0;

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

            while ((newlinePos = receiveBuffer.find(protocol::DELIMITER)) != std::string::npos) {
                size_t wireSize = newlinePos + 1;
                std::string line = receiveBuffer.substr(0, newlinePos);
                receiveBuffer.erase(0, newlinePos + 1);

                // Strip trailing carriage return if present
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                // Check message size limit (MAX_MESSAGE_SIZE = 2048 bytes wire size including DELIMITER)
                if (wireSize > protocol::MAX_MESSAGE_SIZE) {
                    if (!isRegistered) {
                        m_logger.warning("Client registration rejected: message exceeds MAX_MESSAGE_SIZE (" + std::to_string(wireSize) + " > " + std::to_string(protocol::MAX_MESSAGE_SIZE) + ") (" + clientEndpoint + ")");
                        if (protocol::StructuredMessage::isAttemptedStructured(line)) {
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_MESSAGE_TOO_LARGE, "Message exceeds MAX_MESSAGE_SIZE");
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), clientEndpoint, &m_logger);
                        } else {
                            std::string resp = std::string(protocol::RESP_ERR_INVALID_USERNAME) + protocol::DELIMITER;
                            ClientManager::sendAll(clientSocket, resp, clientEndpoint, &m_logger);
                        }
                        shouldExit = true;
                        break;
                    } else {
                        m_logger.warning("Rejected oversized message (" + std::to_string(wireSize) + " bytes) from " + registeredUsername);
                        if (clientProto == protocol::ProtocolVersion::STRUCTURED_V1) {
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_MESSAGE_TOO_LARGE, "Message exceeds MAX_MESSAGE_SIZE");
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                        }
                        continue;
                    }
                }

                // First message must be interpreted as username registration
                if (!isRegistered) {
                    if (protocol::StructuredMessage::isAttemptedStructured(line)) {
                        // Structured Protocol (v1) Registration
                        clientProto = protocol::ProtocolVersion::STRUCTURED_V1;
                        protocol::StructuredMessage reqMsg;
                        std::string parseErr;
                        if (!protocol::StructuredMessage::deserialize(line, reqMsg, parseErr)) {
                            m_logger.warning("Structured registration rejected: JSON parse error (" + parseErr + ") from " + clientEndpoint);
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_MALFORMED_MESSAGE, "Malformed JSON registration: " + parseErr);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), clientEndpoint, &m_logger);
                            shouldExit = true;
                            break;
                        }

                        if (reqMsg.version != protocol::CURRENT_VERSION) {
                            m_logger.warning("Structured registration rejected: unsupported version " + std::to_string(reqMsg.version) + " from " + clientEndpoint);
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_UNSUPPORTED_VERSION, "Unsupported protocol version", reqMsg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), clientEndpoint, &m_logger);
                            shouldExit = true;
                            break;
                        }

                        if (reqMsg.type != protocol::TYPE_REGISTER_REQ) {
                            m_logger.warning("Structured registration rejected: expected register_req but got " + reqMsg.type + " from " + clientEndpoint);
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_UNKNOWN_TYPE, "Expected register_req", reqMsg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), clientEndpoint, &m_logger);
                            shouldExit = true;
                            break;
                        }

                        std::string username = reqMsg.username;
                        RegistrationResult res = m_clientManager.registerClient(
                            clientSocket, username, clientAddr, isClosed, protocol::ProtocolVersion::STRUCTURED_V1);

                        if (res == RegistrationResult::INVALID_USERNAME) {
                            m_logger.warning("Structured registration rejected: invalid username '" + username + "' (" + clientEndpoint + ")");
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_INVALID_USERNAME, "Invalid username format", reqMsg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), clientEndpoint, &m_logger);
                            shouldExit = true;
                            break;
                        } else if (res == RegistrationResult::DUPLICATE_USERNAME) {
                            m_logger.warning("Structured registration rejected: duplicate username '" + username + "' (" + clientEndpoint + ")");
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_USERNAME_TAKEN, "Username '" + username + "' is already taken", reqMsg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), clientEndpoint, &m_logger);
                            shouldExit = true;
                            break;
                        } else if (res != RegistrationResult::SUCCESS) {
                            m_logger.error("Failed to register structured client: " + username + " (" + clientEndpoint + ")");
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_INVALID_USERNAME, "Registration failed", reqMsg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), clientEndpoint, &m_logger);
                            shouldExit = true;
                            break;
                        }

                        // Registration succeeded
                        isRegistered = true;
                        registeredUsername = username;
                        m_logger.info("User registered (Structured v1): " + registeredUsername + " (" + clientEndpoint + ")");
                        m_logger.info("User joined: " + registeredUsername);

                        // Send register_ack
                        auto ack = protocol::StructuredMessage::createRegisterAck(registeredUsername, reqMsg.id);
                        if (!ClientManager::sendAll(clientSocket, ack.serializeWire(), registeredUsername, &m_logger)) {
                            m_logger.error("Failed to send register_ack to " + registeredUsername);
                            shouldExit = true;
                            break;
                        }

                        // Broadcast join notification to all other clients
                        int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
                        std::string legacyJoin = std::string(protocol::PREFIX_SYSTEM) + " " + registeredUsername + " has joined the chat.";
                        auto structJoin = protocol::StructuredMessage::createSystemEvent(
                            protocol::EVENT_JOIN, registeredUsername, legacyJoin, nowSec);
                        m_clientManager.broadcastStructured(structJoin, legacyJoin, clientSocket);

                        continue;
                    } else {
                        // Legacy Protocol (v0) Registration
                        clientProto = protocol::ProtocolVersion::LEGACY_V0;
                        RegistrationResult res = m_clientManager.registerClient(
                            clientSocket, line, clientAddr, isClosed, protocol::ProtocolVersion::LEGACY_V0);

                        if (res == RegistrationResult::INVALID_USERNAME) {
                            m_logger.warning("Client registration rejected: invalid username '" + line + "' (" + clientEndpoint + ")");
                            std::string resp = std::string(protocol::RESP_ERR_INVALID_USERNAME) + protocol::DELIMITER;
                            ClientManager::sendAll(clientSocket, resp, clientEndpoint, &m_logger);
                            shouldExit = true;
                            break;
                        }
                        else if (res == RegistrationResult::DUPLICATE_USERNAME) {
                            m_logger.warning("Client registration rejected: duplicate username '" + line + "' (" + clientEndpoint + ")");
                            std::string resp = std::string(protocol::RESP_ERR_USERNAME_TAKEN) + protocol::DELIMITER;
                            ClientManager::sendAll(clientSocket, resp, clientEndpoint, &m_logger);
                            shouldExit = true;
                            break;
                        }
                        else if (res != RegistrationResult::SUCCESS) {
                            m_logger.error("Failed to register client: " + line + " (" + clientEndpoint + ")");
                            std::string resp = std::string(protocol::RESP_ERR_INVALID_USERNAME) + protocol::DELIMITER;
                            ClientManager::sendAll(clientSocket, resp, clientEndpoint, &m_logger);
                            shouldExit = true;
                            break;
                        }

                        // Registration succeeded
                        isRegistered = true;
                        registeredUsername = line;
                        m_logger.info("User registered (Legacy v0): " + registeredUsername + " (" + clientEndpoint + ")");
                        m_logger.info("User joined: " + registeredUsername);

                        // Send OK acknowledgement directly to the registering client
                        std::string okResp = std::string(protocol::RESP_OK) + protocol::DELIMITER;
                        if (!ClientManager::sendAll(clientSocket, okResp, registeredUsername, &m_logger)) {
                            m_logger.error("Failed to send OK response to " + registeredUsername);
                            shouldExit = true;
                            break;
                        }

                        // Broadcast join notification to all other connected clients
                        int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
                        std::string legacyJoin = std::string(protocol::PREFIX_SYSTEM) + " " + registeredUsername + " has joined the chat.";
                        auto structJoin = protocol::StructuredMessage::createSystemEvent(
                            protocol::EVENT_JOIN, registeredUsername, legacyJoin, nowSec);
                        m_clientManager.broadcastStructured(structJoin, legacyJoin, clientSocket);

                        continue;
                    }
                }

                // Handle post-registration messages
                if (clientProto == protocol::ProtocolVersion::STRUCTURED_V1) {
                    if (!protocol::StructuredMessage::isAttemptedStructured(line)) {
                        m_logger.warning("Malformed message from " + registeredUsername + ": expected JSON object");
                        auto errResp = protocol::StructuredMessage::createError(
                            protocol::ERR_MALFORMED_MESSAGE, "Expected JSON object");
                        ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                        continue;
                    }

                    protocol::StructuredMessage msg;
                    std::string parseErr;
                    if (!protocol::StructuredMessage::deserialize(line, msg, parseErr)) {
                        m_logger.warning("JSON parse error from " + registeredUsername + ": " + parseErr);
                        auto errResp = protocol::StructuredMessage::createError(
                            protocol::ERR_MALFORMED_MESSAGE, "Failed to parse JSON: " + parseErr);
                        ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                        continue;
                    }

                    if (msg.version != protocol::CURRENT_VERSION) {
                        auto errResp = protocol::StructuredMessage::createError(
                            protocol::ERR_UNSUPPORTED_VERSION, "Unsupported protocol version", msg.id);
                        ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                        continue;
                    }

                    if (msg.type == protocol::TYPE_DISCONNECT_REQ) {
                        userRequestedQuit = true;
                        shouldExit = true;
                        break;
                    }
                    else if (msg.type == protocol::TYPE_MSG_SEND) {
                        if (msg.content.empty()) {
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_EMPTY_MESSAGE, "Message content cannot be empty", msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        std::string msgId = "msg-" + std::to_string(++m_messageSequence);
                        int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();

                        // Prepare structured broadcast envelope
                        auto structBroadcast = protocol::StructuredMessage::createMsgBroadcast(
                            registeredUsername, msg.content, msgId, nowSec, msg.to, msg.room);

                        // Enforce wire-size constraint: serialized JSON envelope must remain within MAX_MESSAGE_SIZE
                        if (structBroadcast.serializeWire().size() > protocol::MAX_MESSAGE_SIZE) {
                            m_logger.warning("Message from " + registeredUsername + " rejected: serialized envelope exceeds MAX_MESSAGE_SIZE (" +
                                             std::to_string(structBroadcast.serializeWire().size()) + " > " +
                                             std::to_string(protocol::MAX_MESSAGE_SIZE) + ")");
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_MESSAGE_TOO_LARGE,
                                "Serialized message envelope exceeds MAX_MESSAGE_SIZE wire limit",
                                msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        // Send msg_ack to sender
                        auto ack = protocol::StructuredMessage::createMsgAck(msgId, msg.id);
                        ClientManager::sendAll(clientSocket, ack.serializeWire(), registeredUsername, &m_logger);

                        // Log received normal chat message as a CHAT event
                        m_logger.chat("[" + registeredUsername + "]: " + msg.content);

                        // Broadcast chat message to all other connected clients
                        std::string legacyBroadcast = "[" + registeredUsername + "]: " + msg.content;
                        m_clientManager.broadcastStructured(structBroadcast, legacyBroadcast, clientSocket);
                    }
                    else {
                        m_logger.warning("Unknown message type '" + msg.type + "' from " + registeredUsername);
                        auto errResp = protocol::StructuredMessage::createError(
                            protocol::ERR_UNKNOWN_TYPE, "Unknown message type: " + msg.type, msg.id);
                        ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                        continue;
                    }
                }
                else {
                    // Legacy Protocol (v0) message handling
                    if (line == protocol::CMD_QUIT) {
                        userRequestedQuit = true;
                        shouldExit = true;
                        break;
                    }

                    // Suppress empty chat lines
                    if (line.empty()) {
                        continue;
                    }

                    std::string msgId = "msg-" + std::to_string(++m_messageSequence);
                    int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();

                    // Log received normal chat message as a CHAT event
                    m_logger.chat("[" + registeredUsername + "]: " + line);

                    // Broadcast chat message to all other connected clients
                    std::string legacyBroadcast = "[" + registeredUsername + "]: " + line;
                    auto structBroadcast = protocol::StructuredMessage::createMsgBroadcast(
                        registeredUsername, line, msgId, nowSec);
                    m_clientManager.broadcastStructured(structBroadcast, legacyBroadcast, clientSocket);
                }
            }

            if (shouldExit) {
                break;
            }

            // Bound per-client receive buffer to prevent memory exhaustion without delimiter
            if (receiveBuffer.size() > protocol::MAX_MESSAGE_SIZE) {
                m_logger.warning("Client exceeded MAX_MESSAGE_SIZE without delimiter (" + std::to_string(receiveBuffer.size()) + " bytes) from " + (isRegistered ? registeredUsername : clientEndpoint) + ". Terminating connection.");
                break;
            }
        }
        else if (bytesReceived == 0) {
            // Client closed connection gracefully
            break;
        }
        else {
            int err = netGetLastError();
            if (m_isRunning && !netIsInterrupted(err) && !netIsBadSocket(err) && !netIsConnAbortedOrReset(err)) {
                m_logger.error("recv() failed for " + (isRegistered ? registeredUsername : clientEndpoint)
                               + " with error code: " + std::to_string(err));
            }
            break;
        }
    }

    // Unregister client from ClientManager before broadcasting leave notification
    if (isRegistered) {
        std::string departingUser = registeredUsername;
        bool removed = m_clientManager.removeClient(clientSocket);
        isRegistered = false;

        if (removed) {
            if (userRequestedQuit) {
                m_logger.info("User left: " + departingUser + " (requested disconnect)");
            } else {
                m_logger.info("User disconnected: " + departingUser);
            }

            // Broadcast leave notification to all remaining connected clients
            int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            std::string legacyLeave = std::string(protocol::PREFIX_SYSTEM) + " " + departingUser + " has left the chat.";
            auto structLeave = protocol::StructuredMessage::createSystemEvent(
                protocol::EVENT_LEAVE, departingUser, legacyLeave, nowSec);
            m_clientManager.broadcastStructured(structLeave, legacyLeave, clientSocket);
        }
    }

    // Safely close this worker's client socket exactly once
    if (isClosed && !isClosed->exchange(true)) {
        netShutdownSocket(clientSocket, NET_SHUTDOWN_RDWR);
        netCloseSocket(clientSocket);
    }
}

bool Server::initializeSocket() {
    if (!netInit()) {
        int err = netGetLastError();
        m_logger.error("Network initialization failed with error code: " + std::to_string(err));
        return false;
    }
    m_netInitialized = true;
#if defined(_WIN32)
    m_logger.info("Winsock initialized.");
#else
    m_logger.info("POSIX networking initialized (SIGPIPE ignored).");
#endif

    // Create IPv4 TCP stream socket
    m_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_serverSocket == INVALID_SOCKET) {
        m_logger.error("socket() creation failed with error code: " + std::to_string(netGetLastError()));
        return false;
    }

    // Set SO_REUSEADDR to enable immediate reuse of the local address/port upon restart
    int opt = 1;
    if (setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt)) == SOCKET_ERROR) {
        m_logger.warning("setsockopt(SO_REUSEADDR) failed with error code: " + std::to_string(netGetLastError()));
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
        int err = netGetLastError();
        m_logger.error("bind() failed on port " + std::to_string(m_port) + " with error code: " + std::to_string(err));

        if (netIsAddrInUse(err)) {
            m_logger.error("Diagnosis: Port " + std::to_string(m_port) + " is already in use by another application.");
        } else if (netIsAccessDenied(err)) {
            m_logger.error("Diagnosis: Access denied. Insufficient permissions to bind to port " + std::to_string(m_port) + ".");
        } else if (netIsBadSocket(err)) {
            m_logger.error("Diagnosis: Invalid socket descriptor specified.");
        } else {
            m_logger.error("Diagnosis: Check socket state or network configuration.");
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
        m_logger.error("listen() failed with error code: " + std::to_string(netGetLastError()));
        return false;
    }
    return true;
}

void Server::closeSocket(SOCKET& s) {
    if (s != INVALID_SOCKET) {
        netShutdownSocket(s, NET_SHUTDOWN_RDWR);
        netCloseSocket(s);
        s = INVALID_SOCKET;
    }
}

void Server::cleanup() {
    stop();
}

void Server::stop() {
    if (!m_isRunning.exchange(false)) {
        return;
    }

    m_logger.info("Server shutting down.");

    // 1. Close listening socket so accept() unblocks
    closeSocket(m_serverSocket);

    // 2. Close active client connections so blocking recv() unblocks
    m_clientManager.closeAllClients();

    {
        std::lock_guard<std::mutex> lock(m_threadsMutex);
        for (auto& worker : m_workers) {
            if (worker.isClosed && !worker.isClosed->exchange(true)) {
                netShutdownSocket(worker.socket, NET_SHUTDOWN_RDWR);
                netCloseSocket(worker.socket);
            }
        }
    }

    // 3. Join all tracked worker threads
    {
        std::lock_guard<std::mutex> lock(m_threadsMutex);
        for (auto& worker : m_workers) {
            if (worker.th.joinable()) {
                worker.th.join();
            }
        }
        m_workers.clear();
    }

    // 4. Clean up network subsystem
    if (m_netInitialized.exchange(false)) {
        netCleanup();
    }

    m_logger.info("Server stopped.");
}

bool Server::isRunning() const {
    return m_isRunning;
}

int Server::getPort() const {
    return m_port;
}

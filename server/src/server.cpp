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
    ClientInfo myClientInfo;

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
                            bool isOpRequiringReg = (reqMsg.type == protocol::TYPE_USER_LIST_REQ ||
                                                     reqMsg.type == protocol::TYPE_MSG_DIRECT_REQ ||
                                                     reqMsg.type == protocol::TYPE_ROOM_CREATE_REQ ||
                                                     reqMsg.type == protocol::TYPE_ROOM_JOIN_REQ ||
                                                     reqMsg.type == protocol::TYPE_ROOM_LEAVE_REQ ||
                                                     reqMsg.type == protocol::TYPE_ROOM_LIST_REQ ||
                                                     reqMsg.type == protocol::TYPE_ROOM_MEMBERS_REQ ||
                                                     reqMsg.type == protocol::TYPE_ROOM_MSG_REQ);
                            std::string errCode = isOpRequiringReg ? std::string(protocol::ERR_NOT_REGISTERED)
                                                                   : std::string(protocol::ERR_UNKNOWN_TYPE);
                            std::string errDetail = isOpRequiringReg ? "Client must register before performing operations"
                                                                     : "Expected register_req";
                            auto errResp = protocol::StructuredMessage::createError(
                                errCode, errDetail, reqMsg.id);
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
                        m_clientManager.getClientInfo(clientSocket, myClientInfo);
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
                        m_clientManager.getClientInfo(clientSocket, myClientInfo);
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
                    else if (msg.type == protocol::TYPE_USER_LIST_REQ) {
                        // Check validation errors (e.g. negative page or limit)
                        std::string valErr;
                        if (!msg.validate(valErr)) {
                            m_logger.warning("Invalid user_list_req from " + registeredUsername + ": " + valErr);
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_MALFORMED_MESSAGE, valErr, msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        // Snapshot active registered usernames under mutex (no network I/O under mutex)
                        std::vector<std::string> activeUsers = m_clientManager.getOnlineUsernames();

                        int page = (msg.page > 0) ? msg.page : 1;
                        int limit = (msg.limit > 0) ? msg.limit : protocol::DEFAULT_USER_LIST_LIMIT;
                        if (limit > protocol::MAX_USER_LIST_LIMIT) {
                            limit = protocol::MAX_USER_LIST_LIMIT;
                        }

                        int totalUsers = static_cast<int>(activeUsers.size());
                        int totalPages = (totalUsers + limit - 1) / limit;
                        if (totalPages < 1) totalPages = 1;

                        std::vector<std::string> pageUsers;
                        int startIndex = (page - 1) * limit;
                        if (startIndex < totalUsers) {
                            int endIndex = std::min(startIndex + limit, totalUsers);
                            for (int i = startIndex; i < endIndex; ++i) {
                                pageUsers.push_back(activeUsers[i]);
                            }
                        }

                        auto resp = protocol::StructuredMessage::createUserListResp(
                            pageUsers, totalUsers, page, totalPages, msg.id);

                        // Enforce wire limit
                        if (resp.serializeWire().size() > protocol::MAX_MESSAGE_SIZE) {
                            m_logger.warning("user_list_resp exceeds MAX_MESSAGE_SIZE (" +
                                             std::to_string(resp.serializeWire().size()) + " > " +
                                             std::to_string(protocol::MAX_MESSAGE_SIZE) + ") for " + registeredUsername);
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_MESSAGE_TOO_LARGE,
                                "User list response exceeds wire limit; please request with a smaller page limit",
                                msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        ClientManager::sendAll(clientSocket, resp.serializeWire(), registeredUsername, &m_logger);
                    }
                    else if (msg.type == protocol::TYPE_MSG_DIRECT_REQ) {
                        std::string valErr;
                        if (!msg.validate(valErr)) {
                            m_logger.warning("Invalid msg_direct_req from " + registeredUsername + ": " + valErr);
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_MALFORMED_MESSAGE, valErr, msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        if (!protocol::isValidUsername(msg.to)) {
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_INVALID_USERNAME, "Invalid recipient username format", msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        if (protocol::equalsIgnoreCase(registeredUsername, msg.to)) {
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_CANNOT_MESSAGE_SELF, "Cannot send direct message to yourself", msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        ClientInfo recipientInfo;
                        if (!m_clientManager.getClientByUsername(msg.to, recipientInfo)) {
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_RECIPIENT_NOT_FOUND, "Recipient '" + msg.to + "' is offline or does not exist", msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        if (recipientInfo.protocolVersion != protocol::ProtocolVersion::STRUCTURED_V1) {
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_RECIPIENT_NOT_FOUND, "Recipient does not support private messaging", msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        std::string msgId = "msg-" + std::to_string(++m_messageSequence);
                        int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();

                        auto directMsg = protocol::StructuredMessage::createMsgDirect(
                            registeredUsername, recipientInfo.username, msg.content, msgId, nowSec);

                        if (directMsg.serializeWire().size() > protocol::MAX_MESSAGE_SIZE) {
                            m_logger.warning("Direct message from " + registeredUsername + " to " + recipientInfo.username +
                                             " rejected: exceeds wire limit (" + std::to_string(directMsg.serializeWire().size()) + " bytes)");
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_MESSAGE_TOO_LARGE, "Direct message envelope exceeds wire limit", msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        // Deliver directly to recipient (outside global manager lock, serialized on recipient sendMutex)
                        bool delivered = false;
                        if (recipientInfo.sendMutex && recipientInfo.isClosed) {
                            std::lock_guard<std::mutex> sLock(*recipientInfo.sendMutex);
                            if (!recipientInfo.isClosed->load()) {
                                delivered = ClientManager::sendAll(recipientInfo.socket, directMsg.serializeWire(), recipientInfo.username, &m_logger);
                                if (!delivered) {
                                    if (!recipientInfo.isClosed->exchange(true)) {
                                        netShutdownSocket(recipientInfo.socket, NET_SHUTDOWN_RDWR);
                                        netCloseSocket(recipientInfo.socket);
                                    }
                                }
                            }
                        }

                        if (!delivered) {
                            m_clientManager.removeClient(recipientInfo.socket);
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_RECIPIENT_NOT_FOUND, "Recipient is no longer available", msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        // Send msg_direct_ack to sender
                        auto ack = protocol::StructuredMessage::createMsgDirectAck(msgId, recipientInfo.username, nowSec, msg.id);
                        ClientManager::sendAll(clientSocket, ack.serializeWire(), registeredUsername, &m_logger);

                        m_logger.info("Direct message delivered from " + registeredUsername + " to " + recipientInfo.username + " (msg_id: " + msgId + ")");
                    }
                    else if (msg.type == protocol::TYPE_ROOM_CREATE_REQ) {
                        std::string valErr;
                        if (!msg.validate(valErr)) {
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_MALFORMED_MESSAGE, valErr, msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        RoomMember member;
                        member.socket = clientSocket;
                        member.username = registeredUsername;
                        member.sendMutex = myClientInfo.sendMutex;
                        member.isClosed = myClientInfo.isClosed;
                        member.protocolVersion = clientProto;

                        int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();

                        std::string errCode, errMsg;
                        if (!m_roomManager.createRoom(msg.room, registeredUsername, member, nowSec, errCode, errMsg)) {
                            auto errResp = protocol::StructuredMessage::createError(errCode, errMsg, msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        m_logger.info("Room created: " + msg.room + " by " + registeredUsername);
                        auto resp = protocol::StructuredMessage::createRoomCreateResp(msg.room, protocol::STATUS_OK, msg.id);
                        ClientManager::sendAll(clientSocket, resp.serializeWire(), registeredUsername, &m_logger);
                    }
                    else if (msg.type == protocol::TYPE_ROOM_JOIN_REQ) {
                        std::string valErr;
                        if (!msg.validate(valErr)) {
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_MALFORMED_MESSAGE, valErr, msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        RoomMember member;
                        member.socket = clientSocket;
                        member.username = registeredUsername;
                        member.sendMutex = myClientInfo.sendMutex;
                        member.isClosed = myClientInfo.isClosed;
                        member.protocolVersion = clientProto;

                        std::vector<RoomMember> peerTargets;
                        std::string canonRoom;
                        std::string errCode, errMsg;
                        if (!m_roomManager.joinRoom(msg.room, member, peerTargets, canonRoom, errCode, errMsg)) {
                            auto errResp = protocol::StructuredMessage::createError(errCode, errMsg, msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        m_logger.info("User " + registeredUsername + " joined room " + canonRoom);

                        // Send join response to joining client
                        auto resp = protocol::StructuredMessage::createRoomJoinResp(canonRoom, protocol::STATUS_OK, msg.id);
                        ClientManager::sendAll(clientSocket, resp.serializeWire(), registeredUsername, &m_logger);

                        // Broadcast room_user_joined to existing room members
                        int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
                        auto joinEvent = protocol::StructuredMessage::createRoomUserJoined(canonRoom, registeredUsername, nowSec);
                        std::string joinWire = joinEvent.serializeWire();

                        for (const auto& peer : peerTargets) {
                            if (peer.sendMutex && peer.isClosed) {
                                std::lock_guard<std::mutex> sLock(*peer.sendMutex);
                                if (!peer.isClosed->load()) {
                                    ClientManager::sendAll(peer.socket, joinWire, peer.username, &m_logger);
                                }
                            }
                        }
                    }
                    else if (msg.type == protocol::TYPE_ROOM_LEAVE_REQ) {
                        std::string valErr;
                        if (!msg.validate(valErr)) {
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_MALFORMED_MESSAGE, valErr, msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        std::vector<RoomMember> remainingTargets;
                        std::string canonRoom;
                        std::string errCode, errMsg;
                        if (!m_roomManager.leaveRoom(msg.room, clientSocket, remainingTargets, canonRoom, errCode, errMsg)) {
                            auto errResp = protocol::StructuredMessage::createError(errCode, errMsg, msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        m_logger.info("User " + registeredUsername + " left room " + canonRoom);

                        // Send leave response to leaving client
                        auto resp = protocol::StructuredMessage::createRoomLeaveResp(canonRoom, protocol::STATUS_OK, msg.id);
                        ClientManager::sendAll(clientSocket, resp.serializeWire(), registeredUsername, &m_logger);

                        // Broadcast room_user_left to remaining members
                        int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
                        auto leaveEvent = protocol::StructuredMessage::createRoomUserLeft(canonRoom, registeredUsername, nowSec);
                        std::string leaveWire = leaveEvent.serializeWire();

                        for (const auto& rem : remainingTargets) {
                            if (rem.sendMutex && rem.isClosed) {
                                std::lock_guard<std::mutex> sLock(*rem.sendMutex);
                                if (!rem.isClosed->load()) {
                                    ClientManager::sendAll(rem.socket, leaveWire, rem.username, &m_logger);
                                }
                            }
                        }
                    }
                    else if (msg.type == protocol::TYPE_ROOM_MSG_REQ) {
                        std::string valErr;
                        if (!msg.validate(valErr)) {
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_MALFORMED_MESSAGE, valErr, msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        std::vector<RoomMember> peerTargets;
                        std::string canonRoom;
                        std::string errCode, errMsg;
                        if (!m_roomManager.getRoomPeers(msg.room, clientSocket, peerTargets, canonRoom, errCode, errMsg)) {
                            auto errResp = protocol::StructuredMessage::createError(errCode, errMsg, msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        std::string msgId = "msg-" + std::to_string(++m_messageSequence);
                        int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();

                        auto roomMsg = protocol::StructuredMessage::createRoomMsg(
                            registeredUsername, canonRoom, msg.content, msgId, nowSec);

                        if (roomMsg.serializeWire().size() > protocol::MAX_MESSAGE_SIZE) {
                            m_logger.warning("Room message from " + registeredUsername + " to " + canonRoom + " exceeds wire limit (" +
                                             std::to_string(roomMsg.serializeWire().size()) + " > " + std::to_string(protocol::MAX_MESSAGE_SIZE) + ")");
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_MESSAGE_TOO_LARGE, "Room message envelope exceeds wire limit", msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        // Send room_msg_ack to sender
                        auto ack = protocol::StructuredMessage::createRoomMsgAck(msgId, canonRoom, nowSec, msg.id);
                        ClientManager::sendAll(clientSocket, ack.serializeWire(), registeredUsername, &m_logger);

                        // Broadcast room message to room peers
                        std::string roomWire = roomMsg.serializeWire();
                        for (const auto& peer : peerTargets) {
                            if (peer.sendMutex && peer.isClosed) {
                                std::lock_guard<std::mutex> sLock(*peer.sendMutex);
                                if (!peer.isClosed->load()) {
                                    ClientManager::sendAll(peer.socket, roomWire, peer.username, &m_logger);
                                }
                            }
                        }

                        m_logger.chat("[" + registeredUsername + "@" + canonRoom + "]: " + msg.content);
                    }
                    else if (msg.type == protocol::TYPE_ROOM_LIST_REQ) {
                        std::string valErr;
                        if (!msg.validate(valErr)) {
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_MALFORMED_MESSAGE, valErr, msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        auto activeRooms = m_roomManager.getRoomNames();
                        int page = (msg.page > 0) ? msg.page : 1;
                        int limit = (msg.limit > 0) ? msg.limit : protocol::DEFAULT_ROOM_LIST_LIMIT;
                        if (limit > protocol::MAX_ROOM_LIST_LIMIT) {
                            limit = protocol::MAX_ROOM_LIST_LIMIT;
                        }

                        int totalRooms = static_cast<int>(activeRooms.size());
                        int totalPages = (totalRooms + limit - 1) / limit;
                        if (totalPages < 1) totalPages = 1;

                        std::vector<std::string> pageRooms;
                        int startIndex = (page - 1) * limit;
                        if (startIndex < totalRooms) {
                            int endIndex = std::min(startIndex + limit, totalRooms);
                            for (int i = startIndex; i < endIndex; ++i) {
                                pageRooms.push_back(activeRooms[i]);
                            }
                        }

                        auto resp = protocol::StructuredMessage::createRoomListResp(pageRooms, totalRooms, page, totalPages, msg.id);
                        if (resp.serializeWire().size() > protocol::MAX_MESSAGE_SIZE) {
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_MESSAGE_TOO_LARGE, "Room list response exceeds wire limit", msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        ClientManager::sendAll(clientSocket, resp.serializeWire(), registeredUsername, &m_logger);
                    }
                    else if (msg.type == protocol::TYPE_ROOM_MEMBERS_REQ) {
                        std::string valErr;
                        if (!msg.validate(valErr)) {
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_MALFORMED_MESSAGE, valErr, msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        std::vector<std::string> allMembers;
                        std::string canonRoom;
                        std::string errCode, errMsg;
                        if (!m_roomManager.getRoomMemberNames(msg.room, clientSocket, allMembers, canonRoom, errCode, errMsg)) {
                            auto errResp = protocol::StructuredMessage::createError(errCode, errMsg, msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        int page = (msg.page > 0) ? msg.page : 1;
                        int limit = (msg.limit > 0) ? msg.limit : protocol::DEFAULT_ROOM_MEMBERS_LIMIT;
                        if (limit > protocol::MAX_ROOM_MEMBERS_LIMIT) {
                            limit = protocol::MAX_ROOM_MEMBERS_LIMIT;
                        }

                        int totalMembers = static_cast<int>(allMembers.size());
                        int totalPages = (totalMembers + limit - 1) / limit;
                        if (totalPages < 1) totalPages = 1;

                        std::vector<std::string> pageMembers;
                        int startIndex = (page - 1) * limit;
                        if (startIndex < totalMembers) {
                            int endIndex = std::min(startIndex + limit, totalMembers);
                            for (int i = startIndex; i < endIndex; ++i) {
                                pageMembers.push_back(allMembers[i]);
                            }
                        }

                        auto resp = protocol::StructuredMessage::createRoomMembersResp(
                            canonRoom, pageMembers, totalMembers, page, totalPages, msg.id);
                        if (resp.serializeWire().size() > protocol::MAX_MESSAGE_SIZE) {
                            auto errResp = protocol::StructuredMessage::createError(
                                protocol::ERR_MESSAGE_TOO_LARGE, "Room members response exceeds wire limit", msg.id);
                            ClientManager::sendAll(clientSocket, errResp.serializeWire(), registeredUsername, &m_logger);
                            continue;
                        }

                        ClientManager::sendAll(clientSocket, resp.serializeWire(), registeredUsername, &m_logger);
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

    // Unregister client from ClientManager and RoomManager before broadcasting leave notification
    if (isRegistered) {
        std::string departingUser = registeredUsername;
        bool removed = m_clientManager.removeClient(clientSocket);
        isRegistered = false;

        // Clean up client from all chat rooms and broadcast room_user_left to remaining members
        std::vector<RoomLeaveEvent> roomEvents;
        m_roomManager.removeClientFromAllRooms(clientSocket, roomEvents);

        int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        for (const auto& rEvent : roomEvents) {
            auto roomLeftMsg = protocol::StructuredMessage::createRoomUserLeft(
                rEvent.roomName, departingUser, nowSec);
            std::string roomLeftWire = roomLeftMsg.serializeWire();
            for (const auto& remMember : rEvent.remainingMembers) {
                if (remMember.sendMutex && remMember.isClosed) {
                    std::lock_guard<std::mutex> sLock(*remMember.sendMutex);
                    if (!remMember.isClosed->load()) {
                        ClientManager::sendAll(remMember.socket, roomLeftWire, remMember.username, &m_logger);
                    }
                }
            }
        }

        if (removed) {
            if (userRequestedQuit) {
                m_logger.info("User left: " + departingUser + " (requested disconnect)");
            } else {
                m_logger.info("User disconnected: " + departingUser);
            }

            // Broadcast leave notification to all remaining connected clients
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

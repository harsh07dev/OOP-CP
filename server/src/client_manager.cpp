// client_manager.cpp - Implementation of the ClientManager class.
// Handles thread-safe client registration, lookup, removal, and message broadcasting.

#include "client_manager.h"
#include "logger.h"
#include "protocol.h"
#include "net_compat.h"

#include <iostream>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <string_view>

namespace {
    bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    }
}

ClientManager::ClientManager(Logger* logger)
    : m_logger(logger)
{
}

void ClientManager::setLogger(Logger* logger) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_logger = logger;
}

std::string ClientManager::formatEndpoint(const sockaddr_in& addr) {
    char ipBuffer[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &(addr.sin_addr), ipBuffer, sizeof(ipBuffer)) == nullptr) {
        std::strncpy(ipBuffer, "Unknown", sizeof(ipBuffer) - 1);
    }
    int port = ntohs(addr.sin_port);
    return std::string(ipBuffer) + ":" + std::to_string(port);
}

bool ClientManager::isUsernameTaken(const std::string& username) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& client : m_clients) {
        if (equalsIgnoreCase(client.username, username)) {
            return true;
        }
    }
    return false;
}

RegistrationResult ClientManager::registerClient(SOCKET socket, const std::string& username, const sockaddr_in& address, std::shared_ptr<std::atomic<bool>> isClosed, protocol::ProtocolVersion protocolVersion) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. Verify socket is not already registered
    for (const auto& client : m_clients) {
        if (client.socket == socket) {
            return RegistrationResult::SOCKET_ALREADY_REGISTERED;
        }
    }

    // 2. Validate username format (length, character set, no newlines/control chars)
    if (!protocol::isValidUsername(username)) {
        return RegistrationResult::INVALID_USERNAME;
    }

    // 3. Case-insensitive duplicate username check
    for (const auto& client : m_clients) {
        if (equalsIgnoreCase(client.username, username)) {
            return RegistrationResult::DUPLICATE_USERNAME;
        }
    }

    // 4. Atomically insert client
    ClientInfo info;
    info.socket = socket;
    info.username = username;
    info.address = address;
    info.protocolVersion = protocolVersion;
    if (isClosed) {
        info.isClosed = isClosed;
    }
    m_clients.push_back(info);

    std::cout << "Active clients: " << m_clients.size() << std::endl << std::endl;

    return RegistrationResult::SUCCESS;
}

bool ClientManager::activateClient(SOCKET socket) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& client : m_clients) {
        if (client.socket == socket) {
            client.isActive = true;
            return true;
        }
    }
    return false;
}

bool ClientManager::addClient(SOCKET socket, const std::string& username, const sockaddr_in& address) {
    if (registerClient(socket, username, address) != RegistrationResult::SUCCESS) {
        return false;
    }
    return activateClient(socket);
}

bool ClientManager::removeClient(SOCKET socket) {
    std::shared_ptr<std::mutex> sMutex;
    std::shared_ptr<std::atomic<bool>> sClosed;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        auto it = std::find_if(m_clients.begin(), m_clients.end(), [socket](const ClientInfo& info) {
            return info.socket == socket;
        });

        if (it != m_clients.end()) {
            sMutex = it->sendMutex;
            sClosed = it->isClosed;
            m_clients.erase(it);

            std::cout << "Active clients: " << m_clients.size() << std::endl << std::endl;
        }
    }

    if (sClosed) {
        sClosed->store(true);
    }

    return (sClosed != nullptr);
}

void ClientManager::closeAllClients() {
    std::vector<ClientInfo> clientsCopy;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        clientsCopy = m_clients;
        m_clients.clear();
        std::cout << "Active clients: 0" << std::endl << std::endl;
    }

    for (auto& client : clientsCopy) {
        if (client.sendMutex && client.isClosed) {
            std::lock_guard<std::mutex> lock(*client.sendMutex);
            if (!client.isClosed->exchange(true)) {
                netShutdownSocket(client.socket, NET_SHUTDOWN_RDWR);
                netCloseSocket(client.socket);
            }
        }
    }
}

size_t ClientManager::getClientCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t count = 0;
    for (const auto& client : m_clients) {
        if (client.isActive) {
            ++count;
        }
    }
    return count;
}

bool ClientManager::getClientInfo(SOCKET socket, ClientInfo& outInfo) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (const auto& client : m_clients) {
        if (client.socket == socket) {
            outInfo = client;
            return true;
        }
    }
    return false;
}

bool ClientManager::getClientProtocolVersion(SOCKET socket, protocol::ProtocolVersion& outVersion) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (const auto& client : m_clients) {
        if (client.socket == socket) {
            outVersion = client.protocolVersion;
            return true;
        }
    }
    return false;
}

std::vector<ClientInfo> ClientManager::getAllClients() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_clients;
}

std::vector<std::string> ClientManager::getOnlineUsernames() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> usernames;
    usernames.reserve(m_clients.size());
    for (const auto& client : m_clients) {
        if (client.isActive) {
            usernames.push_back(client.username);
        }
    }
    return usernames;
}

bool ClientManager::getClientByUsername(const std::string& username, ClientInfo& outInfo) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& client : m_clients) {
        if (client.isActive && equalsIgnoreCase(client.username, username)) {
            outInfo = client;
            return true;
        }
    }
    return false;
}

void ClientManager::broadcastMessage(const std::string& message, SOCKET senderSocket) {
    std::vector<ClientInfo> targets;
    Logger* logger = nullptr;

    // Snapshot target clients under the mutex lock to prevent contention during socket I/O
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        logger = m_logger;
        targets.reserve(m_clients.size());
        for (const auto& client : m_clients) {
            if (client.isActive && client.socket != senderSocket) {
                targets.push_back(client);
            }
        }
    }

    std::vector<SOCKET> deadSockets;

    // Perform socket send operations outside the global lock, serialized per client
    for (const auto& target : targets) {
        if (!target.sendMutex || !target.isClosed) {
            continue;
        }

        std::lock_guard<std::mutex> sendLock(*target.sendMutex);
        if (target.isClosed->load()) {
            continue; // Skip already closed socket safely
        }

        bool success = sendAll(target.socket, message, target.username, logger);
        if (!success) {
            if (!target.isClosed->exchange(true)) {
                netShutdownSocket(target.socket, NET_SHUTDOWN_RDWR);
                netCloseSocket(target.socket);
            }
            deadSockets.push_back(target.socket);
        }
    }

    // Evict any dead sockets that failed during broadcast
    if (!deadSockets.empty()) {
        for (SOCKET s : deadSockets) {
            removeClient(s);
        }
    }
}

void ClientManager::broadcastStructured(const protocol::StructuredMessage& structuredMsg, const std::string& legacyText, SOCKET senderSocket) {
    std::vector<ClientInfo> targets;
    Logger* logger = nullptr;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        logger = m_logger;
        targets.reserve(m_clients.size());
        for (const auto& client : m_clients) {
            if (client.isActive && client.socket != senderSocket) {
                targets.push_back(client);
            }
        }
    }

    protocol::StructuredMessage boundedMsg = structuredMsg;
    if (boundedMsg.serializeWire().size() > protocol::MAX_MESSAGE_SIZE) {
        boundedMsg.truncateToFitWire(protocol::MAX_MESSAGE_SIZE);
    }
    std::string structuredWire = boundedMsg.serializeWire();

    std::string legacyWire = legacyText;
    if (legacyWire.empty() || legacyWire.back() != protocol::DELIMITER) {
        legacyWire += protocol::DELIMITER;
    }

    std::vector<SOCKET> deadSockets;

    for (const auto& target : targets) {
        if (!target.sendMutex || !target.isClosed) {
            continue;
        }

        std::lock_guard<std::mutex> sendLock(*target.sendMutex);
        if (target.isClosed->load()) {
            continue;
        }

        const std::string& dataToSend = (target.protocolVersion == protocol::ProtocolVersion::STRUCTURED_V1)
                                        ? structuredWire : legacyWire;

        bool success = sendAll(target.socket, dataToSend, target.username, logger);
        if (!success) {
            if (!target.isClosed->exchange(true)) {
                netShutdownSocket(target.socket, NET_SHUTDOWN_RDWR);
                netCloseSocket(target.socket);
            }
            deadSockets.push_back(target.socket);
        }
    }

    if (!deadSockets.empty()) {
        for (SOCKET s : deadSockets) {
            removeClient(s);
        }
    }
}

bool ClientManager::sendAll(SOCKET sock, const std::string& data, const std::string& username, Logger* logger) {
    if (sock == INVALID_SOCKET) {
        return false;
    }

    const char* dataPtr = data.data();
    size_t totalBytes = data.size();
    size_t totalSent = 0;

    // Reliable partial-send loop ensuring full transmission of the payload
    while (totalSent < totalBytes) {
        int bytesSent = send(sock,
                             dataPtr + totalSent,
                             static_cast<int>(totalBytes - totalSent),
                             MSG_NOSIGNAL);
        if (bytesSent == SOCKET_ERROR) {
            int err = netGetLastError();
            if (logger != nullptr) {
                logger->warning("Failed to broadcast message to " + username 
                                + " (Socket error: " + std::to_string(err) + ")");
            } else {
                std::cerr << "[ClientManager] Failed to send to " << username 
                          << " (Socket error: " << err << ")" << std::endl;
            }
            return false;
        }

        if (bytesSent == 0) {
            if (logger != nullptr) {
                logger->warning("Failed to broadcast message to " + username 
                                + " (send returned 0 bytes)");
            } else {
                std::cerr << "[ClientManager] send() returned 0 bytes to " << username << std::endl;
            }
            return false;
        }

        totalSent += static_cast<size_t>(bytesSent);
    }

    return true;
}

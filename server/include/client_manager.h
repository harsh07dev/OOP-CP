// client_manager.h - Declaration of the ClientManager class and ClientInfo structure.
// Manages thread-safe connected client registration, lookup, removal, and message broadcasting.

#pragma once

#include "net_compat.h"

#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>

// Structure holding connection and identity metadata for an active client
struct ClientInfo {
    SOCKET socket{INVALID_SOCKET};
    std::string username;
    sockaddr_in address{};
    std::shared_ptr<std::mutex> sendMutex{std::make_shared<std::mutex>()};
    std::shared_ptr<std::atomic<bool>> isClosed{std::make_shared<std::atomic<bool>>(false)};
};

enum class RegistrationResult {
    SUCCESS,
    DUPLICATE_USERNAME,
    INVALID_USERNAME,
    SOCKET_ALREADY_REGISTERED
};

class Logger;

class ClientManager {
public:
    explicit ClientManager(Logger* logger = nullptr);
    ~ClientManager() = default;

    // Disable copy semantics to protect the mutex and unique registry state
    ClientManager(const ClientManager&) = delete;
    ClientManager& operator=(const ClientManager&) = delete;

    // Configure logger reference for broadcast error reporting
    void setLogger(Logger* logger);

    // Thread-safe atomic validation, duplicate check (case-insensitive), and registration
    RegistrationResult registerClient(SOCKET socket, const std::string& username, const sockaddr_in& address, std::shared_ptr<std::atomic<bool>> isClosed = nullptr);

    // Thread-safe check if a username is currently taken (case-insensitive)
    bool isUsernameTaken(const std::string& username) const;

    // Thread-safe registration of a new client (delegates to registerClient)
    bool addClient(SOCKET socket, const std::string& username, const sockaddr_in& address);

    // Thread-safe removal of a client by socket handle
    bool removeClient(SOCKET socket);

    // Thread-safe coordinated closure of all active clients (called on server shutdown)
    void closeAllClients();

    // Thread-safe query of active client count
    size_t getClientCount() const;

    // Thread-safe retrieval of specific client information
    bool getClientInfo(SOCKET socket, ClientInfo& outInfo) const;

    // Thread-safe snapshot of all active clients
    std::vector<ClientInfo> getAllClients() const;

    // Thread-safe message broadcasting to all connected clients except senderSocket.
    // Snapshots recipients under the mutex and executes network send() calls outside the lock.
    void broadcastMessage(const std::string& message, SOCKET senderSocket = INVALID_SOCKET);

    // Reliable send helper handling partial sends across TCP byte streams
    static bool sendAll(SOCKET sock, const std::string& data, const std::string& username, Logger* logger = nullptr);

private:
    mutable std::mutex m_mutex;
    std::vector<ClientInfo> m_clients;
    Logger* m_logger{nullptr};

    static std::string formatEndpoint(const sockaddr_in& addr);
};

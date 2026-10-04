// client_manager.h - Declaration of the ClientManager class and ClientInfo structure.
// Manages thread-safe connected client registration, lookup, and removal.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>
#include <vector>
#include <mutex>

// Structure holding connection and identity metadata for an active client
struct ClientInfo {
    SOCKET socket{INVALID_SOCKET};
    std::string username;
    sockaddr_in address{};
};

class ClientManager {
public:
    ClientManager() = default;
    ~ClientManager() = default;

    // Disable copy semantics to protect the mutex and unique registry state
    ClientManager(const ClientManager&) = delete;
    ClientManager& operator=(const ClientManager&) = delete;

    // Thread-safe registration of a new client
    bool addClient(SOCKET socket, const std::string& username, const sockaddr_in& address);

    // Thread-safe removal of a client by socket handle
    bool removeClient(SOCKET socket);

    // Thread-safe query of active client count
    size_t getClientCount() const;

    // Thread-safe retrieval of specific client information
    bool getClientInfo(SOCKET socket, ClientInfo& outInfo) const;

    // Thread-safe snapshot of all active clients (for future broadcasting)
    std::vector<ClientInfo> getAllClients() const;

private:
    mutable std::mutex m_mutex;
    std::vector<ClientInfo> m_clients;

    static std::string formatEndpoint(const sockaddr_in& addr);
};

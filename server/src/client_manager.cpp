// client_manager.cpp - Implementation of the ClientManager class.
// Handles thread-safe client registration, lookup, removal, and message broadcasting.

#include "client_manager.h"

#include <iostream>
#include <algorithm>
#include <cstring>

std::string ClientManager::formatEndpoint(const sockaddr_in& addr) {
    char ipBuffer[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &(addr.sin_addr), ipBuffer, sizeof(ipBuffer)) == nullptr) {
        std::strncpy(ipBuffer, "Unknown", sizeof(ipBuffer) - 1);
    }
    int port = ntohs(addr.sin_port);
    return std::string(ipBuffer) + ":" + std::to_string(port);
}

bool ClientManager::addClient(SOCKET socket, const std::string& username, const sockaddr_in& address) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Verify socket is not already registered
    for (const auto& client : m_clients) {
        if (client.socket == socket) {
            return false;
        }
    }

    ClientInfo info;
    info.socket = socket;
    info.username = username;
    info.address = address;
    m_clients.push_back(info);

    std::cout << "[ClientManager] Registered client: " << username 
              << " (" << formatEndpoint(address) << ")" << std::endl;
    std::cout << "Active clients: " << m_clients.size() << std::endl << std::endl;

    return true;
}

bool ClientManager::removeClient(SOCKET socket) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = std::find_if(m_clients.begin(), m_clients.end(), [socket](const ClientInfo& info) {
        return info.socket == socket;
    });

    if (it != m_clients.end()) {
        std::string username = it->username;
        m_clients.erase(it);

        std::cout << "[ClientManager] Removed client: " << username << std::endl;
        std::cout << "Active clients: " << m_clients.size() << std::endl << std::endl;
        return true;
    }

    return false;
}

size_t ClientManager::getClientCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_clients.size();
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

std::vector<ClientInfo> ClientManager::getAllClients() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_clients;
}

void ClientManager::broadcastMessage(const std::string& message, SOCKET senderSocket) {
    std::vector<ClientInfo> targets;

    // Snapshot target clients under the mutex lock to prevent contention during socket I/O
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        targets.reserve(m_clients.size());
        for (const auto& client : m_clients) {
            if (client.socket != senderSocket) {
                targets.push_back(client);
            }
        }
    }

    // Perform socket send operations outside the mutex lock
    for (const auto& target : targets) {
        sendAll(target.socket, message, target.username);
    }
}

bool ClientManager::sendAll(SOCKET sock, const std::string& data, const std::string& username) {
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
                             0);
        if (bytesSent == SOCKET_ERROR) {
            int err = WSAGetLastError();
            std::cerr << "[ClientManager] Failed to send to " << username 
                      << " (Winsock error: " << err << ")" << std::endl;
            return false;
        }

        if (bytesSent == 0) {
            std::cerr << "[ClientManager] send() returned 0 bytes to " << username << std::endl;
            return false;
        }

        totalSent += static_cast<size_t>(bytesSent);
    }

    return true;
}

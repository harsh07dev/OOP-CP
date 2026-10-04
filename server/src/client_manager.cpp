// client_manager.cpp - Implementation of the ClientManager class.
// Handles thread-safe client registration, lookup, and removal.

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

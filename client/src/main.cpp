// main.cpp - Entry point for the chat client application.
// Prompts user for server IP, port, and username, connects to server, and starts sender/receiver threads.

#include "client.h"

#include <iostream>
#include <string>

int main() {
    std::cout << "================================" << std::endl;
    std::cout << "TCP CHAT CLIENT" << std::endl;
    std::cout << "================================" << std::endl << std::endl;

    std::string ip_input;
    std::cout << "Server IP: ";
    if (!std::getline(std::cin, ip_input) || ip_input.empty()) {
        ip_input = "127.0.0.1";
    }

    std::string port_input;
    std::cout << "Server Port: ";
    int port = 8080;
    if (std::getline(std::cin, port_input) && !port_input.empty()) {
        try {
            port = std::stoi(port_input);
        } catch (...) {
            std::cerr << "[Warning] Invalid port entered. Defaulting to 8080." << std::endl;
            port = 8080;
        }
    }

    std::string username_input;
    std::cout << "Username: ";
    if (!std::getline(std::cin, username_input) || username_input.empty()) {
        username_input = "Anonymous";
    }

    std::cout << std::endl << "Connecting..." << std::endl << std::endl;

    Client client;
    client.set_server_info(ip_input, port);
    client.set_username(username_input);

    if (!client.connect_to_server()) {
        std::cerr << "Failed to connect to server." << std::endl;
        return 1;
    }

    std::cout << "Connected to server." << std::endl;

    // Graceful disconnect on exit
    client.disconnect();

    return 0;
}

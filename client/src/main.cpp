// main.cpp - Entry point for the chat client application.
// Prompts user for server IP, port, and username, connects to server, and starts sender/receiver threads.

#include "client.h"
#include "sender.h"
#include "receiver.h"
#include "protocol_handler.h"
#include "protocol.h"
#include "thread_compat.h"

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

    Client client;
    client.set_server_info(ip_input, port);

    constexpr int MAX_REGISTRATION_ATTEMPTS = 5;
    bool registration_successful = false;
    std::string registered_username;

    for (int attempt = 1; attempt <= MAX_REGISTRATION_ATTEMPTS; ++attempt) {
        // Connect to server if not currently connected
        if (!client.is_connected()) {
            std::cout << "Connecting to server..." << std::endl;
            if (!client.connect_to_server()) {
                std::cerr << "[Error] Failed to connect to server at " << ip_input << ":" << port << std::endl;
                return 1;
            }
            std::cout << "Connected to server." << std::endl;
        }

        // Prompt for username
        if (attempt == 1) {
            std::cout << "Username: ";
        } else {
            std::cout << "Username (attempt " << attempt << " of " << MAX_REGISTRATION_ATTEMPTS << "): ";
        }

        std::string username_input;
        if (!std::getline(std::cin, username_input)) {
            std::cout << "\nInput closed. Exiting." << std::endl;
            client.disconnect();
            return 0;
        }

        client.set_username(username_input);

        // Send username registration request
        if (!client.send_raw(ProtocolHandler::format_join(username_input))) {
            std::cerr << "[Error] Failed to send registration: connection lost." << std::endl;
            client.disconnect();
            return 1;
        }

        // Wait for server's registration response line
        std::string response;
        if (!client.receive_line(response)) {
            std::cerr << "[Error] Server disconnected during registration." << std::endl;
            client.disconnect();
            return 1;
        }

        // Process handshake response
        if (response == protocol::RESP_OK) {
            registration_successful = true;
            registered_username = username_input;
            std::cout << "Registration successful! Welcome, " << registered_username << "." << std::endl << std::endl;
            break;
        }
        else if (response == protocol::RESP_ERR_USERNAME_TAKEN) {
            std::cerr << "[Error] Username '" << username_input 
                      << "' is already taken. Please choose another username." << std::endl;
            client.disconnect();
            if (attempt == MAX_REGISTRATION_ATTEMPTS) {
                std::cerr << "[Error] Maximum registration attempts (" << MAX_REGISTRATION_ATTEMPTS 
                          << ") reached. Exiting." << std::endl;
                return 1;
            }
        }
        else if (response == protocol::RESP_ERR_INVALID_USERNAME) {
            std::cerr << "[Error] Username '" << username_input << "' is invalid." << std::endl;
            std::cerr << "Rules: 1-" << protocol::MAX_USERNAME_LENGTH 
                      << " characters, alphanumeric (a-z, A-Z, 0-9), underscores (_), and hyphens (-) only." << std::endl;
            client.disconnect();
            if (attempt == MAX_REGISTRATION_ATTEMPTS) {
                std::cerr << "[Error] Maximum registration attempts (" << MAX_REGISTRATION_ATTEMPTS 
                          << ") reached. Exiting." << std::endl;
                return 1;
            }
        }
        else {
            std::cerr << "[Error] Unexpected server response during registration: " << response << std::endl;
            client.disconnect();
            return 1;
        }
    }

    if (!registration_successful) {
        return 1;
    }

    std::cout << "----------------------------------------" << std::endl;
    std::cout << "Type your message below." << std::endl;
    std::cout << "Use " << protocol::CMD_QUIT << " to exit." << std::endl;
    std::cout << "----------------------------------------" << std::endl << std::endl;

    Receiver receiver(client);
    Sender sender(client);

    // Launch independent receiver thread
    ChatThread receiver_thread([&receiver]() {
        receiver.start();
    });

    // Launch independent sender thread
    ChatThread sender_thread([&sender]() {
        sender.start();
    });

    // Wait for sender to finish (/quit, stream closure, or disconnect)
    if (sender_thread.joinable()) {
        sender_thread.join();
    }

    // Stop receiver worker and disconnect client
    receiver.stop();
    client.disconnect();

    // Wait for receiver thread to terminate
    if (receiver_thread.joinable()) {
        receiver_thread.join();
    }

    std::cout << "Disconnected from server." << std::endl;
    return 0;
}

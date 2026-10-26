// main.cpp - Entry point for the chat client application.
// Prompts for server IP, port, and username, performs the structured v1
// registration handshake, then starts the sender and receiver workers.

#include "client.h"
#include "chat_session.h"
#include "sender.h"
#include "receiver.h"
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
    ChatSession session(client);

    constexpr int MAX_REGISTRATION_ATTEMPTS = 5;
    bool registration_successful = false;

    for (int attempt = 1; attempt <= MAX_REGISTRATION_ATTEMPTS; ++attempt) {
        if (!client.is_connected()) {
            std::cout << "Connecting to server..." << std::endl;
            if (!client.connect_to_server()) {
                std::cerr << "[Error] Failed to connect to server at " << ip_input << ":" << port << std::endl;
                return 1;
            }
            std::cout << "Connected to server." << std::endl;
        }

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

        std::string detail;
        ChatSession::RegistrationOutcome outcome = session.registerUser(username_input, detail);
        if (outcome == ChatSession::RegistrationOutcome::Success) {
            registration_successful = true;
            std::cout << "Registration successful! Welcome, " << session.username() << "." << std::endl << std::endl;
            break;
        }

        if (outcome == ChatSession::RegistrationOutcome::UsernameTaken) {
            std::cerr << "[Error] Username '" << username_input
                      << "' is already taken. Please choose another username." << std::endl;
        } else if (outcome == ChatSession::RegistrationOutcome::InvalidUsername) {
            std::cerr << "[Error] Username '" << username_input << "' is invalid." << std::endl;
            std::cerr << "Rules: 1-" << protocol::MAX_USERNAME_LENGTH
                      << " characters, alphanumeric (a-z, A-Z, 0-9), underscores (_), and hyphens (-) only." << std::endl;
        } else if (outcome == ChatSession::RegistrationOutcome::ConnectionFailed) {
            std::cerr << "[Error] Server disconnected during registration." << std::endl;
            client.disconnect();
            return 1;
        } else {
            std::cerr << "[Error] Registration failed";
            if (!detail.empty()) {
                std::cerr << ": " << detail;
            }
            std::cerr << std::endl;
        }

        client.disconnect();
        if (attempt == MAX_REGISTRATION_ATTEMPTS) {
            std::cerr << "[Error] Maximum registration attempts (" << MAX_REGISTRATION_ATTEMPTS
                      << ") reached. Exiting." << std::endl;
            return 1;
        }
    }

    if (!registration_successful) {
        return 1;
    }

    std::cout << "----------------------------------------" << std::endl;
    std::cout << "Type a message, or /help to see all commands." << std::endl;
    std::cout << "Use " << protocol::CMD_QUIT << " to exit." << std::endl;
    std::cout << "----------------------------------------" << std::endl << std::endl;

    session.start();

    Receiver receiver(client, session);
    Sender sender(client, session);

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

    // Stop both workers and close the socket, unblocking the receiver if needed.
    session.stop();
    receiver.stop();
    client.disconnect();

    if (receiver_thread.joinable()) {
        receiver_thread.join();
    }

    std::cout << "Disconnected from server." << std::endl;
    return 0;
}

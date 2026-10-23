// client.cpp - Implementation of the Client class.
// Handles TCP socket creation, connecting to server, sending/receiving raw network data, and graceful shutdown.

#include "client.h"
#include "protocol.h"

#include <iostream>
#include <sstream>
#include <cstring>

#ifndef _WIN32
    #define SD_BOTH SHUT_RDWR
#endif

Client::Client()
    : server_ip_("127.0.0.1"),
      server_port_(8080),
      username_("Anonymous"),
      socket_fd_(INVALID_SOCKET_HANDLE),
      is_connected_(false) {
}

Client::~Client() {
    disconnect();
    cleanup_platform_networking();
}

void Client::set_server_info(const std::string& ip, int port) {
    server_ip_ = ip;
    server_port_ = port;
}

void Client::set_username(const std::string& username) {
    username_ = username;
}

const std::string& Client::get_server_ip() const {
    return server_ip_;
}

int Client::get_server_port() const {
    return server_port_;
}

const std::string& Client::get_username() const {
    return username_;
}

bool Client::init_platform_networking() {
#ifdef _WIN32
    if (!wsa_initialized_) {
        WSADATA wsa_data;
        int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (result != 0) {
            std::cerr << "[Error] WSAStartup failed with error: " << result << std::endl;
            return false;
        }
        wsa_initialized_ = true;
    }
#endif
    return true;
}

void Client::cleanup_platform_networking() {
#ifdef _WIN32
    if (wsa_initialized_.exchange(false)) {
        WSACleanup();
    }
#endif
}

void Client::close_socket_handle() {
    socket_handle_t sock = INVALID_SOCKET_HANDLE;
    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        sock = socket_fd_;
        socket_fd_ = INVALID_SOCKET_HANDLE;
    }
    if (sock != INVALID_SOCKET_HANDLE) {
#ifdef _WIN32
        shutdown(sock, SD_BOTH);
        closesocket(sock);
#else
        shutdown(sock, SD_BOTH);
        close(sock);
#endif
    }
}

std::string Client::get_socket_error_message() const {
#ifdef _WIN32
    int error_code = WSAGetLastError();
    std::ostringstream oss;
    oss << "WSA error " << error_code;
    return oss.str();
#else
    return std::string(strerror(errno));
#endif
}

bool Client::connect_to_server() {
    return connect_to_server(server_ip_, server_port_);
}

bool Client::connect_to_server(const std::string& ip, int port) {
    if (is_connected_) {
        disconnect();
    }
    receive_buffer_.clear();

    if (!init_platform_networking()) {
        return false;
    }

    server_ip_ = ip;
    server_port_ = port;

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;        // IPv4
    hints.ai_socktype = SOCK_STREAM;  // TCP
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* addr_result = nullptr;
    std::string port_str = std::to_string(port);

    int resolve_result = getaddrinfo(ip.c_str(), port_str.c_str(), &hints, &addr_result);
    if (resolve_result != 0) {
        std::cerr << "[Error] Address resolution failed for " << ip << ":" << port 
                  << " (" << gai_strerror(resolve_result) << ")" << std::endl;
        cleanup_platform_networking();
        return false;
    }

    bool connected = false;
    std::string last_error = "No route to host";
    for (struct addrinfo* ptr = addr_result; ptr != nullptr; ptr = ptr->ai_next) {
        socket_handle_t temp_sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (temp_sock == INVALID_SOCKET_HANDLE) {
            last_error = get_socket_error_message();
            continue;
        }

        int conn_res = connect(temp_sock, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen));
        if (conn_res == SOCKET_CALL_ERROR) {
            last_error = get_socket_error_message();
#ifdef _WIN32
            shutdown(temp_sock, SD_BOTH);
            closesocket(temp_sock);
#else
            shutdown(temp_sock, SD_BOTH);
            close(temp_sock);
#endif
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(socket_mutex_);
            socket_fd_ = temp_sock;
        }
        connected = true;
        break;
    }

    freeaddrinfo(addr_result);

    if (!connected) {
        std::cerr << "[Error] Connection failed to " << ip << ":" << port 
                  << " - " << last_error << std::endl;
        close_socket_handle();
        is_connected_ = false;
        return false;
    }

    is_connected_ = true;
    return true;
}

void Client::disconnect() {
    is_connected_ = false;
    close_socket_handle();
}

bool Client::is_connected() const {
    return is_connected_;
}

socket_handle_t Client::get_socket() const {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    return socket_fd_;
}

bool Client::send_message(const std::string& message) {
    if (!is_connected_) {
        return false;
    }
    std::string wire_data = message;
    if (wire_data.empty() || wire_data.back() != protocol::DELIMITER) {
        wire_data.push_back(protocol::DELIMITER);
    }
    if (wire_data.size() > protocol::MAX_MESSAGE_SIZE) {
        std::cerr << "[Error] Cannot send message: size (" << wire_data.size()
                  << " bytes) exceeds MAX_MESSAGE_SIZE (" << protocol::MAX_MESSAGE_SIZE << ")." << std::endl;
        return false;
    }
    return send_raw(wire_data);
}

bool Client::send_raw(const std::string& data) {
    socket_handle_t sock = INVALID_SOCKET_HANDLE;
    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        sock = socket_fd_;
    }

    if (!is_connected_ || sock == INVALID_SOCKET_HANDLE) {
        return false;
    }

    size_t total_sent = 0;
    size_t bytes_to_send = data.size();

    while (total_sent < bytes_to_send) {
        int bytes_sent = send(sock, data.data() + total_sent, 
                              static_cast<int>(bytes_to_send - total_sent), 0);
        if (bytes_sent == SOCKET_CALL_ERROR) {
            std::cerr << "[Error] Send failed: " << get_socket_error_message() << std::endl;
            disconnect();
            return false;
        }
        total_sent += static_cast<size_t>(bytes_sent);
    }

    return true;
}

int Client::receive_raw(char* buffer, size_t buffer_size) {
    socket_handle_t sock = INVALID_SOCKET_HANDLE;
    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        sock = socket_fd_;
    }

    if (!is_connected_ || sock == INVALID_SOCKET_HANDLE) {
        return -1;
    }

    int bytes_received = recv(sock, buffer, static_cast<int>(buffer_size), 0);
    if (bytes_received == 0) {
        // Server closed connection gracefully
        disconnect();
        return 0;
    } else if (bytes_received == SOCKET_CALL_ERROR) {
        disconnect();
        return -1;
    }

    return bytes_received;
}

bool Client::receive_line(std::string& out_line) {
    while (is_connected_) {
        // Check if internal receive_buffer_ already contains a complete line
        size_t newline_pos = receive_buffer_.find(protocol::DELIMITER);
        if (newline_pos != std::string::npos) {
            size_t wire_size = newline_pos + 1;
            out_line = receive_buffer_.substr(0, newline_pos);
            receive_buffer_.erase(0, newline_pos + 1);
            if (!out_line.empty() && out_line.back() == '\r') {
                out_line.pop_back();
            }

            // Discard oversized message line safely
            if (wire_size > protocol::MAX_MESSAGE_SIZE) {
                std::cerr << "[Warning] Discarded oversized incoming line (" << wire_size
                          << " bytes, limit is " << protocol::MAX_MESSAGE_SIZE << ")." << std::endl;
                continue;
            }

            return true;
        }

        // Bound incoming line accumulation: prevent buffer from growing unbounded without newline
        if (receive_buffer_.size() > protocol::MAX_MESSAGE_SIZE) {
            std::cerr << "[Error] Incoming data stream exceeded MAX_MESSAGE_SIZE without delimiter ("
                      << receive_buffer_.size() << " bytes). Disconnecting." << std::endl;
            disconnect();
            return false;
        }

        // Need more bytes from network
        constexpr size_t CHUNK_SIZE = 1024;
        char chunk[CHUNK_SIZE];
        int bytes_read = receive_raw(chunk, sizeof(chunk));
        if (bytes_read <= 0) {
            return false;
        }

        receive_buffer_.append(chunk, static_cast<size_t>(bytes_read));
    }
    return false;
}

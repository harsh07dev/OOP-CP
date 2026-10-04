#ifndef CLIENT_H
#define CLIENT_H

// client.h - Declaration of the Client class.
// Manages the client TCP socket, connection parameters, connecting to server, sending/receiving, and disconnection.

#ifdef _WIN32
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0600
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socket_handle_t = SOCKET;
    constexpr socket_handle_t INVALID_SOCKET_HANDLE = INVALID_SOCKET;
    constexpr int SOCKET_CALL_ERROR = SOCKET_ERROR;
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    using socket_handle_t = int;
    constexpr socket_handle_t INVALID_SOCKET_HANDLE = -1;
    constexpr int SOCKET_CALL_ERROR = -1;
#endif

#include <string>
#include <atomic>
#include <cstdint>

class Client {
public:
    Client();
    ~Client();

    // Disable copying to guarantee unique socket ownership
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // Server IP, Port, and Username configuration
    void set_server_info(const std::string& ip, int port);
    void set_username(const std::string& username);

    const std::string& get_server_ip() const;
    int get_server_port() const;
    const std::string& get_username() const;

    // TCP Connection lifecycle
    bool connect_to_server(const std::string& ip, int port);
    bool connect_to_server();
    void disconnect();
    bool is_connected() const;

    // Socket handle management
    socket_handle_t get_socket() const;

    // Modular communication primitives
    bool send_message(const std::string& message);
    bool send_raw(const std::string& data);
    int receive_raw(char* buffer, size_t buffer_size);

private:
    std::string server_ip_;
    int server_port_;
    std::string username_;
    socket_handle_t socket_fd_;
    std::atomic<bool> is_connected_;

    bool init_platform_networking();
    void cleanup_platform_networking();
    void close_socket_handle();
    std::string get_socket_error_message() const;
};

#endif // CLIENT_H

#pragma once

#ifndef NET_COMPAT_H
#define NET_COMPAT_H

// net_compat.h - Cross-platform socket and networking compatibility layer.
// Abstracts platform-specific networking headers, types, error codes, and lifecycle operations
// for Windows (Winsock2) and Linux/POSIX operating systems.

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

using socket_t = SOCKET;

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define NET_SHUTDOWN_RDWR SD_BOTH
#define NET_SHUTDOWN_RD   SD_RECEIVE
#define NET_SHUTDOWN_WR   SD_SEND

inline int netCloseSocket(socket_t s) {
    return ::closesocket(s);
}

inline int netShutdownSocket(socket_t s, int how = NET_SHUTDOWN_RDWR) {
    return ::shutdown(s, how);
}

inline int netGetLastError() {
    return ::WSAGetLastError();
}

inline bool netInit() {
    WSADATA wsaData;
    return (::WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
}

inline void netCleanup() {
    ::WSACleanup();
}

inline bool netIsInterrupted(int err) {
    return err == WSAEINTR;
}

inline bool netIsBadSocket(int err) {
    return err == WSAENOTSOCK || err == WSAEBADF;
}

inline bool netIsConnAbortedOrReset(int err) {
    return err == WSAECONNABORTED || err == WSAECONNRESET;
}

inline bool netIsAddrInUse(int err) {
    return err == WSAEADDRINUSE;
}

inline bool netIsAccessDenied(int err) {
    return err == WSAEACCES;
}

#else // Linux / POSIX

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <csignal>

using socket_t = int;
using SOCKET = int;
using SOCKADDR = struct sockaddr;

#ifndef INVALID_SOCKET
inline constexpr socket_t INVALID_SOCKET = -1;
#endif

#ifndef SOCKET_ERROR
inline constexpr int SOCKET_ERROR = -1;
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define SD_BOTH    SHUT_RDWR
#define SD_RECEIVE SHUT_RD
#define SD_SEND    SHUT_WR

#define NET_SHUTDOWN_RDWR SHUT_RDWR
#define NET_SHUTDOWN_RD   SHUT_RD
#define NET_SHUTDOWN_WR   SHUT_WR

inline int closesocket(socket_t s) {
    return ::close(s);
}

inline int netCloseSocket(socket_t s) {
    return ::close(s);
}

inline int netShutdownSocket(socket_t s, int how = NET_SHUTDOWN_RDWR) {
    return ::shutdown(s, how);
}

inline int netGetLastError() {
    return errno;
}

inline bool netInit() {
    // On POSIX, ignore SIGPIPE so writing to severed client sockets does not kill the process
    std::signal(SIGPIPE, SIG_IGN);
    return true;
}

inline void netCleanup() {
    // No-op on POSIX systems
}

inline bool netIsInterrupted(int err) {
    return err == EINTR;
}

inline bool netIsBadSocket(int err) {
    return err == EBADF || err == ENOTSOCK || err == EINVAL;
}

inline bool netIsConnAbortedOrReset(int err) {
    return err == ECONNABORTED || err == ECONNRESET || err == EPIPE;
}

inline bool netIsAddrInUse(int err) {
    return err == EADDRINUSE;
}

inline bool netIsAccessDenied(int err) {
    return err == EACCES;
}

#endif // _WIN32

#endif // NET_COMPAT_H

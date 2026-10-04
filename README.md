# TCP Multi-Client Chat Application

## 1. Project Overview

This project is a real-time console-based chat application built using C++ TCP sockets. It demonstrates the core principles of network programming and client-server architecture over the Transmission Control Protocol (TCP).

The system consists of:
- One central TCP server
- Multiple TCP clients
- Bi-directional communication
- Multi-client handling
- Message broadcasting

The basic communication model follows a centralized topology:

```text
Client → Server → Client(s)
```

The server acts as the central communication point, coordinating network traffic, managing active connections, and broadcasting incoming text messages from any connected client to all other active clients.

---

## 2. Problem Statement

Design and implement a real-time chat application using TCP sockets in C++. The system should support communication between a single server and multiple clients, allowing text message exchange in real time.

Primary objectives:
- Understand TCP socket programming.
- Establish client-server communication.
- Support multiple clients.
- Enable bi-directional messaging.
- Implement message broadcasting.
- Handle connections reliably.

---

## 3. Phase 1 — Basic Prototype

The current repository is focused on the initial Phase 1 working prototype. Advanced functionality (such as authentication, private rooms, and graphical interfaces) is deferred to subsequent project phases.

The Phase 1 prototype will demonstrate:
- TCP server startup
- TCP client connection
- Server accepting multiple clients
- Client-to-server messaging
- Server-to-client messaging
- Basic broadcasting
- Basic multithreading
- Graceful disconnection
- Basic server-side logging
- Console-based interaction

---

## 4. Basic Architecture

```text
                    TCP CHAT SERVER
                  IP: 192.168.x.x
                     Port: 8080
                         |
             +-----------+-----------+
             |           |           |
             |           |           |
          Client 1    Client 2    Client 3
           Harsh        Rahul        Amit
```

The architectural workflow operates as follows:
1. The server starts and listens on a selected TCP port.
2. A client enters the server IP and port.
3. The client establishes a TCP connection.
4. The server accepts the connection.
5. Multiple clients can connect.
6. A client's message is sent to the server.
7. The server can broadcast the message to other connected clients.

---

## 5. Technologies Used

| Technology | Purpose |
|---|---|
| C++ | Main programming language |
| TCP | Reliable communication protocol |
| Socket API | Network communication |
| C++ Threads | Multi-client handling |
| CMake | Build system |
| Git/GitHub | Version control |

### Platform-Specific Socket APIs

Windows:
- Winsock2

Linux:
- POSIX socket APIs

---

## 6. Important Networking Concepts

### IP Address
Identifies a device on the network.

### Port
Identifies the network service/application on a device.

### Socket
Endpoint used for network communication.

### TCP
Reliable, connection-oriented communication protocol.

### Client
Program that initiates a connection to the server.

### Server
Program that listens for and manages client connections.

### Broadcasting
The server receives a message from one client and forwards it to multiple connected clients.

---

## 7. Project Structure

```text
TCP-Chat-Application/
│
├── server/
│   ├── include/
│   └── src/
│
├── client/
│   ├── include/
│   └── src/
│
├── common/
│   └── include/
│
├── logs/
├── docs/
│
├── CMakeLists.txt
├── .gitignore
└── README.md
```

- `server/`: Contains server header files and source implementations for managing sockets, client connections, listener threads, and logging.
- `client/`: Contains client header files and source implementations for connecting to the server, handling user console input, and managing send/receive threads.
- `common/`: Contains shared header definitions, including protocol constants, packet structures, and message types used by both server and client.
- `logs/`: Directory for runtime log files recording server events, connections, and system activities.
- `docs/`: Holds project documentation, architecture notes, design specifications, and presentation materials.

---

## 8. Team Responsibilities

| Person | Responsibility |
|---|---|
| Person 1 | Server and networking |
| Person 2 | Client and application |

### Person 1 handles:
- Server socket
- Binding
- Listening
- Accepting clients
- Client management
- Threads
- Broadcasting
- Logging

### Person 2 handles:
- Client socket
- Server connection
- User input
- Sending messages
- Receiving messages
- Client interface
- Sender/receiver threads

Both sides share the protocol defined in:
`common/include/protocol.h`

---

## 9. How the Prototype Will Work

Message flow:

```text
User enters message
        ↓
Client
        ↓
TCP
        ↓
Server
        ↓
Broadcast
        ↓
Other Clients
```

Example interaction:

```text
Harsh:
Hello everyone!

        ↓

Server receives:
"Hello everyone!"

        ↓

Server broadcasts:

        ┌──────────────→ Rahul
Server ─┼──────────────→ Amit
        └──────────────→ Rohan
```

---

## 10. Example User Experience

The following terminal sessions illustrate the **expected prototype behavior** once development of Phase 1 is complete. These outputs represent planned prototype interactions, not currently finalized behavior.

### Expected Server Terminal Output

```text
================================
        TCP CHAT SERVER
================================

Server started.
Listening on port 8080...

Client connected.
Client connected.

[10:30:12] Harsh: Hello everyone!
[10:30:18] Rahul: Hi Harsh!
```

### Expected Client Terminal Output

```text
================================
        TCP CHAT CLIENT
================================

Server IP: 192.168.1.10
Server Port: 8080
Username: Harsh

Connected to server.

[10:30:12] Harsh: Hello everyone!
[10:30:18] Rahul: Hi Harsh!

>
```

---

## 11. Build and Run

The project uses CMake as the intended cross-platform build system.

Conceptual build and run workflow:

```text
Create build directory
        ↓
Run CMake
        ↓
Build project
        ↓
Run chat_server
        ↓
Run one or more chat_client instances
```

*Note: Specific build scripts and invocation commands will be documented and verified as each platform target is validated.*

---

## 12. Testing Plan

The Phase 1 prototype will be verified against the following functional test cases:

### Test 1 — Server Startup
Verify that the server starts and listens on the selected port.

### Test 2 — Single Client
Verify that one client can connect.

### Test 3 — Bi-directional Communication
Verify that client and server can exchange messages.

### Test 4 — Multiple Clients
Connect two or more clients simultaneously.

### Test 5 — Broadcasting
Send a message from Client 1 and verify that other clients receive it.

### Test 6 — Disconnection
Disconnect a client and verify that the server handles it without crashing.

### Test 7 — Multiple Messages
Send multiple messages rapidly and verify stable communication.

---

## 13. Current Scope vs Future Scope

The right column represents planned future work and is **not** part of the current Phase 1 prototype.

| Phase 1 | Future Phases |
|---|---|
| TCP communication | Private messaging |
| Multiple clients | Chat rooms |
| Broadcasting | Authentication |
| Basic threads | Heartbeat/PING-PONG |
| Console interface | GUI |
| Basic logging | Persistent chat history |
| Basic commands | Encryption |
| Graceful disconnect | Server statistics |
| Shared protocol | Advanced administration |

---

## 14. Development Roadmap

```text
Phase 1
Basic TCP Connection
        ↓
Phase 2
Multi-client Communication
        ↓
Phase 3
Broadcasting + User Management
        ↓
Phase 4
Private Messaging + Commands
        ↓
Phase 5
Chat Rooms + Advanced Features
        ↓
Phase 6
Testing + Optimization + Final Demo
```

---

## 15. Course Requirements Mapping

| Course Requirement | Planned Implementation |
|---|---|
| TCP/IP sockets | TCP socket API |
| Bi-directional chat | send() + recv() |
| Multiple clients | C++ threads |
| Broadcasting | Server-side message forwarding |
| Server logs | Logger module |
| Error handling | Socket/connection error handling |
| Modularity | Separate server/client/common modules |

---

## 16. Future Improvements

The following improvements are planned for future iterations of the project:
- Private messaging
- Chat rooms
- Authentication
- Message IDs
- Heartbeat mechanism
- Server statistics
- Better command system
- GUI client
- Encryption
- Persistent chat history
- Performance testing

---

## 17. Project Status

```text
Phase: Phase 1 — Basic Prototype
Status: Structure created / Development in progress
```

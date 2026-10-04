# TCP Multi-Client Chat Application

## Phase 1 Description

This phase implements a basic console-based TCP chat prototype demonstrating:
- One central TCP server
- Multiple concurrent TCP clients
- Bidirectional TCP communication (Client → Server, Server → Client)
- Multithreading for handling multiple client connections concurrently
- Message broadcasting from one client to all other connected clients
- Basic timestamped event logging

## Architecture

```text
Client 1 ─┐
Client 2 ─┼──> TCP Server
Client 3 ─┘
```

## Build Instructions

### Prerequisites
- CMake 3.15 or newer
- C++17 compliant compiler (MSVC, GCC, Clang)

### Building with CMake

```bash
# Create and navigate to the build directory
mkdir build
cd build

# Generate build files
cmake ..

# Compile the project
cmake --build .
```

## Run Instructions

### Start the Server
In Terminal 1:
```bash
./chat_server
```

### Start Clients
In Terminal 2:
```bash
./chat_client
```

In Terminal 3:
```bash
./chat_client
```

## Example Usage

### Server Output
```text
Server started
Listening on port 8080
[2026-10-05 00:15:35] Client connected: Harsh
[2026-10-05 00:15:42] Message received: Hello everyone!
```

### Client Output
```text
Server IP: 127.0.0.1
Server Port: 8080
Username: Harsh

Connected successfully.
Type your messages below (/quit to exit):
```

## Phase 1 Limitations

The following advanced features are intentionally excluded from Phase 1 and will be introduced in subsequent phases:
- Private messaging
- Chat rooms / Channels
- User authentication & registration
- Database integration
- Graphical User Interface (GUI)
- End-to-end encryption
- Heartbeat / PING-PONG keepalive
- Server statistics & admin commands
- Persistent chat history
- Docker containerization & Web frontend

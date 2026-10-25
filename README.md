# TCP Multi-Client Chat Application

A cross-platform C++17 console chat application built on TCP sockets. The
server supports multiple concurrent clients, public chat, private messages,
ephemeral chat rooms, SQLite-backed message history, and both a structured
JSON protocol (v1) and the original newline-delimited legacy protocol (v0).

## Features

### Core chat

- TCP server with per-client worker threads
- Multiple simultaneous clients
- Public broadcast chat with sender exclusion
- Username registration with validation
- Case-insensitive duplicate username rejection
- Graceful `/quit` disconnect and server shutdown
- Timestamped, thread-safe logging to `logs/chat.log`
- 2048-byte wire-frame limit with stream buffering
- Per-socket send serialization (no interleaved frames)
- Cross-platform networking (Windows Winsock / POSIX sockets)

### Advanced features

- Structured Protocol v1 with JSON envelopes, correlation IDs, and message IDs
- Private direct messaging with delivery isolation
- Ephemeral chat rooms with membership, listing, and member listing
- Persistent public, direct, and room message history via SQLite
- Cursor-based history pagination (`before_id` / `after_id`)
- Online user listing with pagination
- Legacy v0 clients remain fully compatible with public chat

## Project structure

```text
.
├── client/
│   ├── include/           # Client, session, receiver, sender, console helpers
│   └── src/
├── common/
│   └── include/           # Shared protocol constants, messages, JSON parser
├── server/
│   ├── include/           # Server, client manager, rooms, message store, logger
│   └── src/
├── docs/
│   ├── PROTOCOL_SPECIFICATION.md
│   └── PROJECT_PROGRESS_AND_PHASE2_READINESS.md
├── logs/
├── tests/
│   └── regression_tests.py
├── CMakeLists.txt
└── README.md
```

## Requirements

- CMake 3.15 or newer
- A C++17 compiler
- SQLite3 development library
- Python 3 (only for the regression suite)

### Install prerequisites

macOS (Homebrew):

```bash
brew install cmake sqlite
```

Ubuntu / Debian:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libsqlite3-dev python3
```

Windows:

- Install MinGW-w64 or Visual Studio with C++ support.
- Install CMake.
- Install SQLite3 development files and make sure they are discoverable by
  CMake (or adjust `SQLite3_ROOT`).

## Build

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The executables are written to:

- `build/chat_server` (`build/chat_server.exe` on Windows)
- `build/chat_client` (`build/chat_client.exe` on Windows)

### Direct compiler builds

Linux / macOS:

```bash
g++ -std=c++17 -Wall -Wextra -pthread -Icommon/include -Iserver/include \
    server/src/main.cpp server/src/server.cpp server/src/client_manager.cpp \
    server/src/room_manager.cpp server/src/message_store.cpp server/src/logger.cpp \
    -lsqlite3 -o chat_server

g++ -std=c++17 -Wall -Wextra -pthread -Icommon/include -Iclient/include \
    client/src/main.cpp client/src/client.cpp client/src/chat_session.cpp \
    client/src/sender.cpp client/src/receiver.cpp \
    -o chat_client
```

Windows (MinGW):

```bash
g++ -std=c++17 -Wall -Wextra -Icommon/include -Iserver/include \
    server/src/main.cpp server/src/server.cpp server/src/client_manager.cpp \
    server/src/room_manager.cpp server/src/message_store.cpp server/src/logger.cpp \
    -lws2_32 -lsqlite3 -o chat_server.exe

g++ -std=c++17 -Wall -Wextra -Icommon/include -Iclient/include \
    client/src/main.cpp client/src/client.cpp client/src/chat_session.cpp \
    client/src/sender.cpp client/src/receiver.cpp \
    -lws2_32 -o chat_client.exe
```

## Run

Start the server:

```bash
./build/chat_server
```

The default port is `8080`; logs are written to `logs/chat.log`.
You can also choose a port and database path:

```bash
./build/chat_server 9090 ./chat_history.db
```

Start one or more clients in separate terminals:

```bash
./build/chat_client
```

Enter the server IP (default `127.0.0.1`), port (default `8080`), and username.
Usernames must be 1–32 characters and may contain letters, digits, `_`, and `-`.

## Client commands

Anything that does not start with `/` is sent as a public chat message.

| Command | Description |
|---|---|
| `/help` | Show all commands |
| `/whoami` | Show your username |
| `/status` | Show connection information |
| `/users [page] [limit]` | List online users |
| `/msg <user> <message>` | Send a private message (alias `/dm`) |
| `/create <room>` | Create and join a room |
| `/join <room>` | Join an existing room |
| `/leave <room>` | Leave a room |
| `/rooms [page] [limit]` | List active rooms |
| `/members <room> [page] [limit]` | List room members |
| `/room <room> <message>` | Send a room message (alias `/rmsg`) |
| `/history` | Show recent public history |
| `/history public [limit] [before\|after <msg-id>]` | Public history with pagination |
| `/history dm <user> [limit] [before\|after <msg-id>]` | Direct-message history |
| `/history room <room> [limit] [before\|after <msg-id>]` | Room history |
| `/quit` | Disconnect and exit |

Room names must be 1–32 characters and may contain letters, digits, `_`, `-`,
and `#`. Rooms are ephemeral: when the last member leaves, the room is removed.

## Example session

```text
$ ./build/chat_client
================================
TCP CHAT CLIENT
================================

Server IP: 127.0.0.1
Server Port: 8080
Username: Alice
Connecting to server...
Connected to server.
Registration successful! Welcome, Alice.

----------------------------------------
Type a message, or /help to see all commands.
Use /quit to exit.
----------------------------------------

> hello everyone
[You] hello everyone
> /users
--- Online users (page 1/1, total 2) ---
  Alice
  Bob

> /msg Bob this is private
[DM to Bob] this is private
> /create lobby
[Room] Created room 'lobby'.
> /room lobby welcome to the lobby
[You @lobby] welcome to the lobby
> /quit
Disconnecting from server...
Disconnected from server.
```

## Testing

Run the full regression suite from the repository root:

```bash
python3 tests/regression_tests.py
```

The suite covers:

- Clean compilation
- Multi-client registration, broadcasting, and sender exclusion
- Duplicate and invalid usernames
- Message framing, fragmentation, coalescing, and size limits
- Concurrent broadcasts and thread safety
- Server shutdown and restart
- Structured protocol registration, acknowledgements, and errors
- Private messaging, rooms, and membership isolation
- SQLite persistence, restart recovery, and cursor pagination
- Interactive client commands and output
- Cross-protocol compatibility between structured and legacy clients

## Protocol

The full wire specification is in
[`docs/PROTOCOL_SPECIFICATION.md`](docs/PROTOCOL_SPECIFICATION.md).

Key invariants:

- Newline-delimited framing (`\n`)
- Maximum frame size: 2048 bytes
- Current structured protocol version: `1`
- UTF-8 text with JSON escaping for embedded newlines
- Legacy v0 registration: `<username>\n`, reply `OK\n` or an error token
- Structured v1 registration: `register_req` → `register_ack`

## Project status

**Core MVP: complete.** The original Phase 1 requirements — TCP server,
multiple clients, bidirectional messaging, broadcasting, threading, graceful
disconnect, and logging — are implemented and regression-tested.

**Current scope: Phase 4 complete.** Structured protocol, private messaging,
chat rooms, and persistent message history are implemented on the server and
exposed through the console client.

Remaining work is outside the current MVP scope:

- Authentication and password-based accounts
- Heartbeat / keep-alive detection
- TLS transport encryption
- Graphical client
- Server administration and moderation tools
- Deployment packaging

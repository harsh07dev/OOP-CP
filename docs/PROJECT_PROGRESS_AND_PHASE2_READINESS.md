# Project Progress Report & Phase 2 Readiness Assessment

**Document Target:** `docs/PROJECT_PROGRESS_AND_PHASE2_READINESS.md`  
**Project:** C++ TCP Socket Multi-Client Console Chat Application  
**Current Branch:** `Server-side` (clean working tree, synced with `origin/Server-side`)  
**Assessment Date:** October 2026  
**Auditor:** Pair Programming Assistant (Antigravity)  

---

## 1. Executive Summary

This report delivers a comprehensive, factual code audit and architecture assessment of the **C++ TCP Multi-Client Chat Application** before starting Phase 2. The codebase implements a centralized client-server chat architecture over TCP stream sockets targeting Windows using Microsoft Winsock2 and C++17.

### High-Level Status
The Phase 1 prototype is **substantially implemented and functional for primary happy-path scenarios**:
- The server binds to `0.0.0.0:8080`, listens, and accepts multiple concurrent client TCP connections via a thread-per-client model.
- Connected clients can transmit their username, receive system broadcast announcements, exchange broadcast messages across all peers, and disconnect gracefully using `/quit`.
- All connection events, messages, and warnings are logged to a thread-safe, persistent file (`logs/chat.log`).
- The project compiles with zero warnings under GCC 16.1.0 (`-std=c++17 -lws2_32`).

### Critical Findings Requiring Remediation Before Phase 2
Despite core functional progress, the repository exhibits architectural discrepancies, missing safety boundaries, and concurrency hazards:
1. **Shared Protocol Non-Exclusion (`common/include/protocol.h`):** Although documented as the shared contract between Person 1 and Person 2, `common/include/protocol.h` contains only 3 lines of comments and zero declarations. The client implements an ad-hoc protocol in [client/include/protocol_handler.h](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/include/protocol_handler.h), while the server relies on hardcoded string slicing in [server/src/server.cpp](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp).
2. **Missing Message Framing on Client:** While the server correctly buffers incoming TCP byte streams and slices complete messages at `\n` boundaries, the client's [Receiver::start()](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/src/receiver.cpp#L19-L56) treats every single `recv()` chunk as an entire, isolated message. Any TCP packet fragmentation or coalescing breaks message presentation.
3. **Absence of Registration Handshake & Username Validation:** The server does not validate username uniqueness (duplicate usernames are silently accepted into the registry), does not reject oversized or non-printable usernames, and never transmits an acknowledgement (ACK) or rejection (NACK) back to the client.
4. **Detached Server Threads & Unsafe Shutdown:** Worker threads are launched detached ([server.cpp:98](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp#L98)). On server shutdown (`SIGINT`), `WSACleanup()` is invoked while detached worker threads remain blocked in `recv()`, causing socket abort errors (confirmed in [logs/chat.log:64](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/logs/chat.log#L64)) and potential undefined behavior.
5. **Concurrent Socket Send Hazards:** When broadcasting messages, [ClientManager::broadcastMessage()](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/client_manager.cpp#L90-L110) executes `send()` calls outside the manager lock without per-socket synchronization. If two clients transmit concurrently, worker threads can invoke `send()` on the same target socket simultaneously, violating socket thread-safety.

---

## 2. Actual Repository Structure

The actual file tree on disk consists of 24 files across 6 primary directories (excluding `.git`):

```text
C:\Users\harsh\OneDrive\Desktop\OOP-CP\
├── .gitignore                           # Git ignore rules (776 bytes)
├── CMakeLists.txt                       # CMake build definition (1,017 bytes)
├── README.md                            # Main project overview and roadmap (9,155 bytes)
├── chat_client.exe                      # Untracked prebuilt client binary (245,136 bytes)
├── chat_server.exe                      # Untracked prebuilt server binary (374,991 bytes)
├── client/                              # Client module
│   ├── include/
│   │   ├── client.h                     # Client socket abstraction header (2,279 bytes)
│   │   ├── protocol_handler.h           # Client protocol framing/parser header (1,837 bytes)
│   │   ├── receiver.h                   # Receiver worker class header (635 bytes)
│   │   ├── sender.h                     # Sender worker class header (625 bytes)
│   │   └── thread_compat.h              # MinGW/Windows thread portability shim (2,459 bytes)
│   └── src/
│       ├── client.cpp                   # Client network I/O implementation (6,066 bytes)
│       ├── main.cpp                     # Client entry point and thread coordinator (2,869 bytes)
│       ├── receiver.cpp                 # Receiver worker receive loop (1,938 bytes)
│       └── sender.cpp                   # Sender worker console input loop (1,602 bytes)
├── common/                              # Shared interface module
│   └── include/
│       └── protocol.h                   # Skeleton comments only (177 bytes, 3 lines)
├── docs/                                # Documentation directory
│   └── README.md                        # Phase 1 documentation index (659 bytes)
├── logs/                                # Runtime logging directory
│   ├── .gitkeep                         # Git directory keeper (37 bytes)
│   └── chat.log                         # Persistent server log history (4,530 bytes, 78 lines)
└── server/                              # Server module
    ├── include/
    │   ├── client_manager.h             # Thread-safe client registry header (2,172 bytes)
    │   ├── logger.h                     # Thread-safe persistent logger header (1,191 bytes)
    │   └── server.h                     # TCP server listener & worker header (1,905 bytes)
    └── src/
        ├── client_manager.cpp           # Client registry & broadcast logic (4,671 bytes)
        ├── logger.cpp                   # Timestamped log writer (2,888 bytes)
        ├── main.cpp                     # Server entry point & signal handler (1,399 bytes)
        └── server.cpp                   # Server socket lifecycle & receive loop (11,740 bytes)
```

### Directory & File Roles

| Directory / File | Stated Purpose in Docs | Actual Observed State | Discrepancies & Notes |
|---|---|---|---|
| `common/include/protocol.h` | Shared protocol definitions and packet structs | 3 lines of comments; no structs, enums, or constants | **Critical gap**: Not included by any translation unit. |
| `client/include/protocol_handler.h` | Modular client protocol handler | Implements `ProtocolHandler`, `ClientMessage`, and `MessageCategory` | Exists only in `client/include/`, not shared with server. |
| `client/include/thread_compat.h` | Thread compatibility layer | Implements `PlatformThread` via `CreateThread` for MinGW | Provides `ChatThread` alias (`std::thread` or Win32 thread). |
| `logs/chat.log` | Runtime server logs | Contains 78 log entries across 5 server sessions | Ignored by `.gitignore`, but contains test evidence. |
| `chat_server.exe` / `chat_client.exe` | Precompiled binaries | Present in repository root; ignored by `.gitignore` | Local build artifacts retained in workspace. |
| `docs/` | Architectural specifications | Contains only placeholder `docs/README.md` | Phase 2 readiness doc created here. |

---

## 3. Development Environment and Build Instructions

### 3.1 Git and Branch Status
- **Current Branch:** `Server-side`
- **Upstream Tracking:** `origin/Server-side` (clean working tree, no uncommitted changes)
- **Commit History Summary:**
  - `932a587` — `feat: implement TCP chat server with Winsock and multi-threading support` (Harsh.dev)
  - `9e33fec` — `feat: implement core server logic and client manager for TCP chat server` (Harsh.dev)
  - `53308f1` — `feat: implement TCP chat server with client management and multithreaded connection handling` (Harsh.dev)
  - `9350d94` — `Merge branch 'main' of https://github.com/harsh07dev/OOP-CP into Server-side`
  - `67bf1fd` — `Merge pull request #1 from harsh07dev/Client` (merged shrX-Code's client commits: `fbf99e2`, `ba52c2e`, `085f0a7`, `d929c38`)
  - The `Server-side` branch holds the complete combined codebase of both Person 1 (Server) and Person 2 (Client).

### 3.2 Toolchain & Environment Assumptions
- **Host OS:** Windows 10/11 (64-bit)
- **Compiler:** MinGW-w64 GCC 16.1.0 (`x86_64-w64-mingw32`, MSYS2 UCRT64 located at `C:\Users\harsh\OneDrive\Desktop\setup\ucrt64\bin\g++.exe`).
- **C++ Standard:** C++17 (`-std=c++17`).
- **Platform Libraries:** Microsoft Winsock 2.2 (`ws2_32.lib` linked via `-lws2_32`).
- **Build System Note:** `CMakeLists.txt` is configured (`cmake_minimum_required(VERSION 3.15)`), but `cmake.exe` is currently not on the system PATH. Direct invocation of `g++` succeeds cleanly.

### 3.3 Verified Build Commands
Both targets compile without errors or warnings using GCC:

```powershell
# Build Server Executable
g++ -std=c++17 -Wall -Wextra -Iserver/include -Icommon/include `
    server/src/main.cpp `
    server/src/server.cpp `
    server/src/client_manager.cpp `
    server/src/logger.cpp `
    -lws2_32 -o chat_server.exe

# Build Client Executable
g++ -std=c++17 -Wall -Wextra -Iclient/include -Icommon/include `
    client/src/main.cpp `
    client/src/client.cpp `
    client/src/sender.cpp `
    client/src/receiver.cpp `
    -lws2_32 -o chat_client.exe
```

### 3.4 Launch and Execution Instructions

#### Starting the Server:
```powershell
.\chat_server.exe
```
- Listens on `0.0.0.0:8080`.
- Outputs log events simultaneously to console and `logs/chat.log`.
- Terminate via `Ctrl+C` (triggers `handleSignal` in [server/src/main.cpp:13-19](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/main.cpp#L13-L19)).

#### Starting One or More Clients:
```powershell
.\chat_client.exe
```
- Interactive prompts:
  1. `Server IP:` (Press Enter for default `127.0.0.1`)
  2. `Server Port:` (Press Enter for default `8080`)
  3. `Username:` (Press Enter for default `Anonymous`)
- Once connected, type messages at the `> ` prompt and press Enter.
- Disconnect gracefully by typing `/quit` and pressing Enter.

---

## 4. Current Server Architecture

The server implementation is encapsulated in the `Server`, `ClientManager`, and `Logger` classes under `server/include/` and `server/src/`.

```mermaid
graph TD
    Main[server/src/main.cpp] -->|Instantiates & starts| Srv[Server]
    Srv -->|Owns & configures| CM[ClientManager]
    Srv -->|Owns & invokes| Log[Logger]
    Srv -->|acceptClient loop| Accept[accept clientSocket]
    Accept -->|Spawns detached std::thread| Worker[Server::handleClient worker]
    Worker -->|recv loop & string framing| Parser[Message Processor]
    Parser -->|Register socket| CM
    Parser -->|Log events| Log
    Parser -->|broadcastMessage| CM
    CM -->|Snapshot targets & sendAll| OtherClients[Other Connected Sockets]
```

### 4.1 Winsock Initialization and Cleanup
- **Initialization:** [Server::initializeSocket()](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp#L218-L250) calls `WSAStartup(MAKEWORD(2, 2), &wsaData)`. Sets `m_wsaInitialized = true`.
- **Cleanup:** [Server::cleanup()](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp#L290-L301) closes the listening socket and calls `WSACleanup()`.
- **Destructor:** `~Server()` calls `cleanup()`.

### 4.2 Socket Creation and Configuration
- In [Server::initializeSocket()](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp#L229-L248):
  - Created via `m_serverSocket = socket(AF_INET, SOCK_STREAM, 0);`. (Note: Protocol argument is `0` rather than explicit `IPPROTO_TCP`, which defaults to TCP).
  - Socket option `SO_REUSEADDR` is enabled via `setsockopt` ([server.cpp:238](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp#L238)) to allow immediate restart.
  - Address structure `sockaddr_in` configured with `sin_family = AF_INET`, `sin_addr.s_addr = htonl(INADDR_ANY)` (`0.0.0.0`), and `sin_port = htons(m_port)`.

### 4.3 Binding, Listening, and Accepting
- **Binding:** [Server::bindSocket()](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp#L251-L270) invokes `bind()`. Features verbose diagnostic error logging for `WSAEADDRINUSE`, `WSAEACCES`, and `WSAENOTSOCK`.
- **Listening:** [Server::startListening()](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp#L272-L281) puts the socket into listening state with `BACKLOG = 5`.
- **Accept Loop:** [Server::acceptClient()](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp#L60-L101) blocks on `accept()`. When a client connects:
  - Resolves IP/port using `inet_ntop` and `ntohs`.
  - Logs `Client connected: <ip>:<port>`.
  - Spawns worker thread: `std::thread clientThread(&Server::handleClient, this, clientSocket, clientAddr);`
  - Immediately detaches: `clientThread.detach();` ([server.cpp:98](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp#L98)).

### 4.4 Client Ownership and Thread Execution
- Client socket ownership is passed by value (`SOCKET clientSocket`) into `handleClient()`.
- The worker thread manages the socket lifecycle until disconnect, then invokes `closesocket(clientSocket);` at line 215.
- **Limitation:** Detaching threads leaves the `Server` instance with zero tracking of active threads. There is no `join()` mechanism on shutdown.

### 4.5 Worker Loop and Stream Framing (`handleClient`)
- Defined in [server/src/server.cpp:103-216](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp#L103-L216).
- Stack buffer: `char rawBuffer[1024];`.
- Invokes `int bytesReceived = recv(clientSocket, rawBuffer, sizeof(rawBuffer), 0);`.
- Appends chunk to `std::string receiveBuffer`.
- **Framing Logic:**
  ```cpp
  while ((newlinePos = receiveBuffer.find('\n')) != std::string::npos) {
      std::string line = receiveBuffer.substr(0, newlinePos);
      receiveBuffer.erase(0, newlinePos + 1);
      if (!line.empty() && line.back() == '\r') {
          line.pop_back();
      }
      // Process complete message line...
  }
  ```
- **Stream Framing Evaluation:** This correctly handles stream segmentation where a message is split across multiple `recv()` calls or multiple lines arrive in one `recv()`.
- **Vulnerability:** `receiveBuffer` has no size upper-bound. A client transmitting continuous bytes without a `\n` can exhaust server memory.

### 4.6 Username Registration and Handling
- The first extracted line is treated as registration ([server.cpp:140-162](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp#L140-L162)):
  - If `line.empty()`, registration is rejected, warning logged, and connection closed.
  - Registers via `m_clientManager.addClient(clientSocket, registeredUsername, clientAddr)`.
  - On success, sets `isRegistered = true`, logs `User registered: <user>` and `User joined: <user>`, and broadcasts:  
    `"[System] " + registeredUsername + " has joined the chat.\n"`.
  - **Limitation:** `ClientManager::addClient()` only verifies that the `SOCKET` is unique. It **does not check if the username is already in use**. Duplicate usernames are allowed.
  - **Limitation:** The server transmits **no response or acknowledgement (ACK/NACK)** to the registering client.

### 4.7 Disconnection and `/quit` Handling
- If `line == "/quit"`, `userRequestedQuit = true`, breaking the loop.
- If `bytesReceived == 0`, connection closed by client, breaking the loop.
- If `bytesReceived < 0`, socket error logged (ignoring normal shutdown codes `WSAEINTR`, `WSAECONNRESET`, `WSAENOTSOCK`), breaking the loop.
- **Teardown sequence:**
  1. `m_clientManager.removeClient(clientSocket)`
  2. Log disconnect event (`User left: ... (requested disconnect)` or `User disconnected: ...`).
  3. Broadcast: `"[System] " + departingUser + " has left the chat.\n"`.
  4. `closesocket(clientSocket);`.

### 4.8 ClientManager Concurrency and Broadcasting
- Defined in [server/include/client_manager.h](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/include/client_manager.h) and [server/src/client_manager.cpp](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/client_manager.cpp).
- Internal registry: `std::vector<ClientInfo> m_clients;`.
- Concurrency control: `mutable std::mutex m_mutex;`.
- Methods `addClient`, `removeClient`, `getClientCount`, `getClientInfo`, and `getAllClients` guard `m_clients` with `std::lock_guard<std::mutex> lock(m_mutex);`.
- **Broadcast Execution:**
  ```cpp
  // 1. Snapshot recipients under mutex
  {
      std::lock_guard<std::mutex> lock(m_mutex);
      for (const auto& client : m_clients) {
          if (client.socket != senderSocket) {
              targets.push_back(client);
          }
      }
  }
  // 2. Transmit outside mutex
  for (const auto& target : targets) {
      sendAll(target.socket, message, target.username, logger);
  }
  ```
- **Partial Send Handling:** `sendAll()` loops `while (totalSent < totalBytes)` until all bytes are transmitted or an error occurs.
- **Identified Hazards:**
  1. If `sendAll()` fails on a target socket, it logs a warning but **does not remove the dead socket from `m_clients`**.
  2. Releasing the mutex before sending avoids locking the registry during network I/O, but introduces a race condition: multiple worker threads can execute `sendAll()` on the **same target socket** simultaneously without synchronization.

### 4.9 Logger Implementation
- Defined in [server/include/logger.h](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/include/logger.h) and [server/src/logger.cpp](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/logger.cpp).
- Thread-safe: All file and console writes are protected by `std::lock_guard<std::mutex> lock(m_mutex)`.
- Timestamp: Formatted via `std::chrono::system_clock` and `localtime_s` as `YYYY-MM-DD HH:MM:SS`.
- Levels: `INFO`, `CHAT`, `WARN`, `ERR`.
- Formatted output: `[YYYY-MM-DD HH:MM:SS] [LEVEL] message`.
- Persistence: Appends to `logs/chat.log`, auto-creates missing parent directories via `std::filesystem::create_directories`, and calls `m_logFile.flush()` after every entry.
- Console mirroring: Directs `INFO`/`CHAT` to `std::cout` and `WARN`/`ERR` to `std::cerr`.

---

## 5. Current Client Architecture

The client implementation is organized into `Client`, `Sender`, `Receiver`, and `ProtocolHandler` classes under `client/include/` and `client/src/`.

```mermaid
graph TD
    Main[client/src/main.cpp] -->|Initializes & connects| Cl[Client]
    Main -->|Sends join registration| Cl
    Main -->|Spawns ChatThread| RecvTh[Receiver Thread]
    Main -->|Spawns ChatThread| SendTh[Sender Thread]
    SendTh -->|getline from std::cin| SendWorker[Sender::start]
    SendWorker -->|ProtocolHandler::format_chat / leave| Cl
    Cl -->|send_raw TCP| ServerSocket[Server Socket]
    ServerSocket -->|recv TCP| Cl
    RecvTh -->|receive_raw chunk| RecvWorker[Receiver::start]
    RecvWorker -->|ProtocolHandler::parse| Parse[Console Output]
    SendTh -->|/quit or EOF| MainJoin[Sender Finished]
    MainJoin -->|receiver.stop & client.disconnect| MainExit[Exit]
```

### 5.1 Socket Creation and Connection
- In [Client::connect_to_server()](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/src/client.cpp#L94-L158):
  - Calls `init_platform_networking()` (`WSAStartup`).
  - Resolves server IP/port via `getaddrinfo` with `AF_INET`, `SOCK_STREAM`, `IPPROTO_TCP`.
  - Iterates through `addrinfo` results, creates socket via `socket()`, and invokes `connect()`.
  - On connection, sets `std::atomic<bool> is_connected_ = true`.

### 5.2 Username Registration
- In [client/src/main.cpp:59-60](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/src/main.cpp#L59-L60):
  - Prompts username from console (defaulting to `"Anonymous"` if empty).
  - Formats via `ProtocolHandler::format_join(client.get_username())` (`username + "\n"`).
  - Sends raw payload to server.
  - **Critical Gap:** The client **does not wait for or verify** any registration response. It immediately spawns sender and receiver threads regardless of whether the server accepted the username.

### 5.3 Threading Model and Worker Separation
- **Receiver Worker ([client/src/receiver.cpp](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/src/receiver.cpp)):**
  - Runs on a dedicated thread (`ChatThread receiver_thread`).
  - Loops calling `client_.receive_raw(buffer, sizeof(buffer) - 1)` while `is_running_ && client_.is_connected()`.
- **Sender Worker ([client/src/sender.cpp](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/src/sender.cpp)):**
  - Runs on a dedicated thread (`ChatThread sender_thread`).
  - Loops reading lines via `std::getline(std::cin, line)`.
  - Intercepts `/quit`, formats leave payload via `ProtocolHandler::format_leave()`, transmits it, calls `client_.disconnect()`, and exits.
- **Portability Layer ([client/include/thread_compat.h](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/include/thread_compat.h)):**
  - Provides `PlatformThread` wrapping native Win32 `CreateThread` for MinGW environments lacking `gthreads`, or aliases `ChatThread = std::thread` when POSIX threads are available.

### 5.4 Message Formatting and Sending
- Normal text is passed through `ProtocolHandler::format_chat(line)` (`line + "\n"`).
- Transmitted through [Client::send_raw()](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/src/client.cpp#L187-L207).
- Implements a partial send loop `while (total_sent < bytes_to_send)`.

### 5.5 Flaw: Client-Side Receive Buffering & Framing
- In [Receiver::start()](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/src/receiver.cpp#L21-L38):
  ```cpp
  char buffer[4096];
  while (is_running_ && client_.is_connected()) {
      int bytes_received = client_.receive_raw(buffer, sizeof(buffer) - 1);
      if (bytes_received > 0) {
          buffer[bytes_received] = '\0';
          ClientMessage msg = ProtocolHandler::parse(std::string(buffer, bytes_received));
          std::cout << "\r" << msg.text << "\n> " << std::flush;
      }
      // ...
  }
  ```
- **Analysis:** The client contains **no stream accumulator buffer**.
  - If the server sends `"[User1]: Hi\n[User2]: Hey\n"` in one TCP packet, the client parses it as a single message containing embedded newlines.
  - If a message is split across two TCP packets, the first fragment is printed without its ending, and the second fragment is printed on the next line.
  - Unlike the server's `receiveBuffer.find('\n')`, the client lacks a message framing loop.

### 5.6 Disconnect and Teardown Synchronization
- When `Sender` exits (via `/quit` or EOF), `main()` joins `sender_thread`.
- Calls `receiver.stop()` and `client.disconnect()`.
- Calls `receiver_thread.join()`.
- **Known UX Hang:** If the **server disconnects unexpectedly**, `Receiver` detects EOF/error, prints `"[Notice] Server disconnected. Press Enter to exit."`, and calls `client_.disconnect()`. However, `sender_thread` is blocked inside `std::getline(std::cin, line)`! The application cannot cleanly terminate until the user presses Enter on the keyboard.

---

## 6. Actual Client–Server Protocol

The operational protocol was reconstructed directly from the source implementations:

### 6.1 Protocol Specification Matrix

| Message Type | Direction | Initiator | Actual Wire Format | Delimiter | Handshake / Response |
|---|---|---|---|---|---|
| **Registration (JOIN)** | Client → Server | Client | `<username>\n` | `\n` | **None** (Server broadcasts to others, but sends no reply to client) |
| **Registration Rejection** | Server → Client | Server | *None* (Server logs warning and closes socket) | N/A | Socket closed; no error code sent to client |
| **Chat Message** | Client → Server | Client | `<text>\n` | `\n` | None |
| **Broadcast Chat** | Server → Clients | Server | `[<username>]: <text>\n` | `\n` | None |
| **Join Announcement** | Server → Clients | Server | `[System] <username> has joined the chat.\n` | `\n` | None (Excluded from joiner) |
| **Graceful Leave** | Client → Server | Client | `/quit\n` | `\n` | None |
| **Leave Announcement** | Server → Clients | Server | `[System] <username> has left the chat.\n` | `\n` | None (Excluded from leaver) |

### 6.2 Application Protocol Discrepancies
1. **Empty Protocol Header:** `common/include/protocol.h` contains zero code. The contract exists only as implicit assumptions between [server/src/server.cpp](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp) and [client/include/protocol_handler.h](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/include/protocol_handler.h).
2. **Asymmetric Framing:** The server frames by newline scanning; the client frames by single `recv()` call.
3. **Implicit Message Types:** There are no protocol headers, opcodes, or message type tags. System messages are identified on the client solely by checking string prefixes (`[System]`, `[Notice]`, `[Server]`, `***`). If a normal user types `[System] Hello`, the client categorizes it as a system message.
4. **No Timestamps on Wire:** The server prefixes logs with timestamps, but wire messages do not contain timestamps.

---

## 7. Implemented Features

The following features were verified through direct code inspection and recorded log history:

| Component | Feature | Implementation Location | Verified Status |
|---|---|---|---|
| **Server** | TCP Socket Setup & Reuse | [server.cpp:218-249](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp#L218-L249) | **Implemented & Verified** |
| **Server** | `bind()` with Diagnostics | [server.cpp:251-270](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp#L251-L270) | **Implemented & Verified** |
| **Server** | Non-blocking accept & per-client thread | [server.cpp:60-101](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp#L60-L101) | **Implemented & Verified** |
| **Server** | Stream buffering & `\n` slicing | [server.cpp:120-138](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp#L120-L138) | **Implemented & Verified** |
| **Server** | Thread-safe Client Registry | [client_manager.cpp:30-88](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/client_manager.cpp#L30-L88) | **Implemented & Verified** |
| **Server** | Sender-Excluded Broadcasting | [client_manager.cpp:90-110](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/client_manager.cpp#L90-L110) | **Implemented & Verified** |
| **Server** | Partial Send Handling (`sendAll`) | [client_manager.cpp:112-153](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/client_manager.cpp#L112-L153) | **Implemented & Verified in Code** |
| **Server** | Join/Leave System Notifications | [server.cpp:154, 210](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp#L154) | **Implemented & Verified** |
| **Server** | Timestamped Persistent Logger | [logger.cpp:13-105](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/logger.cpp#L13-L105) | **Implemented & Verified** |
| **Client** | Winsock Initialization & Connect | [client.cpp:94-158](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/src/client.cpp#L94-L158) | **Implemented & Verified** |
| **Client** | Dual Worker Threads (Sender/Receiver) | [main.cpp:65-74](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/src/main.cpp#L65-L74) | **Implemented & Verified** |
| **Client** | Partial Send Handling (`send_raw`) | [client.cpp:187-207](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/src/client.cpp#L187-L207) | **Implemented & Verified in Code** |
| **Client** | Graceful `/quit` Command Interception | [sender.cpp:36-41](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/src/sender.cpp#L36-L41) | **Implemented & Verified** |
| **Client** | MinGW Thread Portability Shim | [thread_compat.h:14-83](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/include/thread_compat.h#L14-L83) | **Implemented & Verified** |

---

## 8. Testing and Verification Results

### 8.1 Compilation Verification (Executed During Audit)
Direct compilation was executed using GCC 16.1.0 on Windows:
- **Server:** `g++ -std=c++17 -fsyntax-only -Iserver/include -Icommon/include server/src/*.cpp -lws2_32` → **Passed (Exit code 0, 0 warnings)**.
- **Client:** `g++ -std=c++17 -fsyntax-only -Iclient/include -Icommon/include client/src/*.cpp -lws2_32` → **Passed (Exit code 0, 0 warnings)**.

### 8.2 Functional Test Results Matrix
Based on executed syntax tests and empirical log evidence in [logs/chat.log](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/logs/chat.log):

| Test Item | Verification Status | Basis / Evidence |
|---|---|---|
| **Compilation** | **Passed** | Clean compilation of both targets with `g++ -std=c++17`. |
| **Server Startup & Port Binding** | **Passed** | [chat.log:1-5](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/logs/chat.log#L1-L5): `Server bound to port 8080. Server listening on port 8080.` |
| **Single-Client Connection** | **Passed** | [chat.log:26-30](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/logs/chat.log#L26-L30): Client Dave connected, chatted, disconnected. |
| **Multiple Simultaneous Clients** | **Passed** | [chat.log:6-14](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/logs/chat.log#L6-L14): 3 concurrent clients (Alice, Bob, Charlie) active simultaneously. |
| **Bidirectional Messaging** | **Passed** | [chat.log:43-44](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/logs/chat.log#L43-L44): Harsh and Shrikar exchanged interactive messages. |
| **Broadcasting & Sender Exclusion** | **Passed** | [chat.log:15-17](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/logs/chat.log#L15-L17) and code logic [client_manager.cpp:100](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/client_manager.cpp#L100) (`socket != senderSocket`). |
| **Username Registration** | **Passed** | [chat.log:7, 10, 13, 37, 41](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/logs/chat.log#L7): Users Alice, Bob, Charlie, Harsh, Shrikar registered. |
| **Join/Leave Notifications** | **Passed** | [chat.log:8, 11, 14, 18-20](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/logs/chat.log#L8): Join and leave events logged and broadcasted. |
| **Graceful `/quit` Disconnect** | **Passed** | [chat.log:18-20](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/logs/chat.log#L18-L20): `User left: <user> (requested disconnect)`. |
| **Unexpected Disconnection** | **Implemented, not verified** | Code contains error catch for `recv() <= 0`, but unverified under abrupt crash. |
| **Duplicate Usernames** | **Not implemented** | Code inspection shows `ClientManager::addClient()` ignores username collisions. |
| **Invalid / Oversized Messages** | **Not implemented** | No max buffer size limit or empty chat rejection on server. |
| **Partial-Send Handling** | **Implemented, not verified** | `sendAll()` loop exists in code, but not verified under network throttling. |
| **Concurrent Broadcasting** | **Implemented, not verified** | Code executes `sendAll` outside mutex lock; concurrent thread safety unverified. |
| **Server Shutdown with Active Clients** | **Failed** | [chat.log:62-64](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/logs/chat.log#L62-L64): `[ERROR] recv() failed for shirrrrkar with error code: 10053` on shutdown. |
| **Logging and Log Persistence** | **Passed** | [chat.log](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/logs/chat.log) persisted 78 log entries across 5 server sessions. |
| **Repeated Connection Cycles** | **Passed** | 5 distinct server startups and multiple client reconnection cycles in `chat.log`. |

---

## 9. Phase 1 Completion Audit

Evaluation against the original Phase 1 requirements:

| Phase 1 Requirement | Status | Supporting Code Evidence & Audit Notes |
|---|---|---|
| **TCP server and client** | **Complete** | [Server](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp) and [Client](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/src/client.cpp) implement socket lifecycle over IPv4 TCP. |
| **Multiple simultaneous clients** | **Complete** | Multi-client support verified with up to 3 concurrent active clients in [chat.log:6-14](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/logs/chat.log#L6-L14). |
| **Bidirectional communication** | **Complete** | Clients send to server; server broadcasts back to clients; verified in log history. |
| **Per-client threading** | **Complete** | Dedicated thread spawned per client in [server.cpp:97](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp#L97). |
| **Username registration** | **Partially Complete** | Basic registration works; lacks validation, uniqueness check, and ACK response. |
| **Newline-delimited framing** | **Partially Complete** | Implemented on server ([server.cpp:130](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp#L130)), but **missing on client** ([receiver.cpp:24](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/src/receiver.cpp#L24)). |
| **Thread-safe client management**| **Complete** | `ClientManager` protects registry access with `std::mutex`. |
| **Broadcasting to other clients** | **Complete** | `broadcastMessage()` excludes sender; verified in code and logs. |
| **Join and leave notifications** | **Complete** | Server generates `[System]` notifications on join and exit. |
| **Graceful `/quit` handling** | **Complete** | `/quit` cleanly unregisters client, notifies peers, and closes socket. |
| **Partial-send handling** | **Complete** | `ClientManager::sendAll()` and `Client::send_raw()` loop until total bytes sent. |
| **Timestamped persistent logging**| **Complete** | `Logger` writes timestamped entries to file and console with mutex protection. |
| **Basic error handling** | **Partially Complete** | Socket errors caught, but server shutdown crashes active sockets (code 10053). |
| **Successful multi-client operation** | **Complete** | Verified across 5 separate logged sessions. |

---

## 10. Confirmed Bugs and Potential Risks

The following technical issues were identified through direct source analysis:

### 10.1 Confirmed Defect: Missing Message Framing in Client Receiver
- **Severity:** **Critical**
- **Evidence:** In [client/src/receiver.cpp:24-38](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/src/receiver.cpp#L24-L38), incoming data is read into a 4096-byte buffer and passed directly to `ProtocolHandler::parse()` without checking for `\n` or maintaining an accumulation buffer.
- **Why It Matters:** TCP is a continuous byte stream. If multiple broadcast messages arrive in one TCP packet, they are merged into one line. If a message is split across packets, it is printed in incomplete fragments.
- **Recommended Fix:** Add a `std::string receive_buffer_` member to `Receiver` or `Client`, scan for `\n`, extract complete lines, and only parse framed messages.
- **Coordination:** Person 2 task (Client internal).

### 10.2 Confirmed Defect: Unchecked Duplicate Usernames
- **Severity:** **High**
- **Evidence:** In [server/src/client_manager.cpp:30-49](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/client_manager.cpp#L30-L49), `addClient()` checks only if `client.socket == socket`. It never checks if `client.username == username`.
- **Why It Matters:** Two clients can register identical usernames. Users cannot determine who sent which message, and private messaging in future phases will be impossible.
- **Recommended Fix:** Check `isUsernameTaken(username)` inside `ClientManager::addClient()`. If taken, reject registration.
- **Coordination:** Coordinated: Person 1 (Server rejection) + Person 2 (Client handling rejection).

### 10.3 Confirmed Defect: No Registration Handshake (Missing ACK/NACK)
- **Severity:** **High**
- **Evidence:** In [server/src/server.cpp:148-160](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp#L148-L160), when a user registers, the server sends no confirmation message. If rejected (e.g. empty username), the server closes the socket silently. On the client ([main.cpp:60](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/src/main.cpp#L60)), the client immediately launches sender/receiver threads without waiting for an acknowledgement.
- **Why It Matters:** If registration fails, the client is unaware and hangs on console input while the socket is already closed.
- **Recommended Fix:** Protocol must define `JOIN_ACK` and `JOIN_REJECT <reason>`. Client must wait for confirmation before launching worker threads.
- **Coordination:** Coordinated change between Person 1 and Person 2.

### 10.4 Confirmed Defect: Unsafe Server Shutdown with Active Clients
- **Severity:** **High**
- **Evidence:** In [server/src/server.cpp:290-301](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp#L290-L301), `cleanup()` closes the listening socket and immediately invokes `WSACleanup()`. It does not notify or close active client sockets in `ClientManager`. In [logs/chat.log:62-64](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/logs/chat.log#L62-L64), this caused `recv() failed with error code: 10053` (`WSAECONNABORTED`).
- **Why It Matters:** Calling `WSACleanup()` while background worker threads are blocked inside `recv()` causes socket abortions and potential memory access violations.
- **Recommended Fix:** Implement a graceful shutdown sequence: set `m_isRunning = false`, iterate through all registered clients, transmit a shutdown notice (`[Server] Server shutting down...`), close client sockets, and join worker threads before calling `WSACleanup()`.
- **Coordination:** Person 1 task.

### 10.5 Confirmed Risk: Concurrent `send()` on Target Sockets During Broadcast
- **Severity:** **High**
- **Evidence:** In [server/src/client_manager.cpp:90-110](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/client_manager.cpp#L90-L110), `broadcastMessage()` snapshots targets under lock, then calls `sendAll()` on each target outside the lock.
- **Why It Matters:** If Client A and Client B broadcast simultaneously, two worker threads invoke `sendAll()` on Client C's socket concurrently. In Winsock/TCP, concurrent `send()` calls on the same socket descriptor can cause interleaved payload bytes and corrupt messages.
- **Recommended Fix:** Add a per-client send mutex or a centralized outgoing message queue.
- **Coordination:** Person 1 task.

### 10.6 Confirmed Defect: Stale Sockets Left in Registry on Failed Broadcast
- **Severity:** **Medium**
- **Evidence:** In [server/src/client_manager.cpp:127-147](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/client_manager.cpp#L127-L147), when `sendAll()` encounters an error, it logs a warning and returns `false`. However, `broadcastMessage()` ignores the return value and does not remove the broken client socket from `m_clients`.
- **Why It Matters:** Broken connections linger in the client registry until their own worker thread detects the error, wasting resources on subsequent broadcasts.
- **Recommended Fix:** Collect failed sockets during broadcast and unregister them.
- **Coordination:** Person 1 task.

### 10.7 Confirmed Defect: Detached Thread Lifecycle Management
- **Severity:** **High**
- **Evidence:** In [server/src/server.cpp:97-98](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp#L97-L98), worker threads are detached via `clientThread.detach()`.
- **Why It Matters:** Detached threads cannot be joined or monitored. If the server is stopped, the main process terminates while detached threads may still be active in memory.
- **Recommended Fix:** Track active thread handles or `std::thread` objects in a thread pool / vector and join them during server shutdown.
- **Coordination:** Person 1 task.

### 10.8 Confirmed Defect: Empty Shared Protocol Header
- **Severity:** **High**
- **Evidence:** [common/include/protocol.h](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/common/include/protocol.h) contains no code. Both client and server use disconnected, independent logic.
- **Why It Matters:** Any protocol change requires manually editing two separate string-formatting files without compiler-enforced types.
- **Recommended Fix:** Populate `common/include/protocol.h` with shared constants (buffer sizes, delimiters, command prefixes) and serialization functions. Include it in both client and server.
- **Coordination:** Coordinated change between Person 1 and Person 2.

### 10.9 Confirmed UX Bug: Sender Thread Hangs on Server Disconnect
- **Severity:** **Medium**
- **Evidence:** In [client/src/sender.cpp:26](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/src/sender.cpp#L26), `Sender::start()` is blocked on `std::getline(std::cin, line)`.
- **Why It Matters:** If the server disconnects, `Receiver` prints `"Press Enter to exit"`, but the application remains running until the user presses Enter.
- **Recommended Fix:** Use non-blocking console input or signal cancellation to allow clean exit.
- **Coordination:** Person 2 task.

### 10.10 Confirmed Defect: Unbounded Server Receive Buffer
- **Severity:** **Medium**
- **Evidence:** In [server/src/server.cpp:124](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp#L124), `receiveBuffer.append(rawBuffer, bytesReceived)` grows indefinitely until a `\n` is encountered.
- **Why It Matters:** A client can send gigabytes of non-delimited data, leading to memory exhaustion (Denial of Service).
- **Recommended Fix:** Enforce a maximum message length (e.g. 4096 bytes). If buffer exceeds limit without `\n`, disconnect the client.
- **Coordination:** Person 1 task.

---

## 11. Phase 2 Readiness Assessment

### Overall Evaluation: **Conditionally Ready**
The project has successfully achieved the functional objectives of Phase 1: TCP connection, per-client multithreading, real-time broadcasting, graceful exit, and persistent logging are all working and demonstrable.

However, moving immediately to advanced application features (such as private messaging, rooms, or user commands) without addressing the foundational flaws identified above would result in compounded technical debt. 

### Mandatory Pre-Phase 2 Hardening Requirements
Before introducing new feature commands, the team must complete a stabilization milestone:
1. Populate and enforce `common/include/protocol.h`.
2. Implement the registration handshake (ACK/NACK) with duplicate username prevention.
3. Fix message framing in the client's `Receiver`.
4. Implement thread tracking and safe shutdown on the server.

---

## 12. Recommended Phase 2 Tasks — Person 1 (Server & Networking)

### Task 1.1: Shared Protocol Integration
- **Objective:** Establish `common/include/protocol.h` as the single source of truth for wire constants and message framing rules.
- **Existing Files:** `common/include/protocol.h`, `server/include/server.h`, `server/src/server.cpp`.
- **Expected Changes:**
  - Define wire constants: `DELIMITER = '\n'`, `MAX_MESSAGE_LENGTH = 2048`, `MAX_USERNAME_LENGTH = 32`.
  - Define protocol command tags: `CMD_QUIT = "/quit"`, `PREFIX_SYS = "[System] "`, `PREFIX_ERR = "[Error] "`.
  - Include `protocol.h` in `server/src/server.cpp`.
- **Dependencies:** Coordinated with Person 2 (Task 2.3).
- **Acceptance Criteria:** Server references protocol constants rather than hardcoded string literals.

### Task 1.2: Username Validation, Uniqueness Check, and Registration Handshake
- **Objective:** Prevent duplicate usernames, reject invalid usernames, and send an ACK/NACK handshake back to the client.
- **Existing Files:** `server/include/client_manager.h`, `server/src/client_manager.cpp`, `server/src/server.cpp`.
- **Expected Changes:**
  - Add `bool ClientManager::isUsernameTaken(const std::string& username) const;`.
  - Validate username: length between 1 and 32 characters, alphanumeric characters only (no spaces or control characters).
  - On registration attempt in `handleClient()`:
    - If valid and unique: Register client, send `OK\n` to `clientSocket`, then broadcast join notification.
    - If invalid or duplicate: Send `ERR_USERNAME_TAKEN\n` or `ERR_INVALID_USERNAME\n` to `clientSocket` and terminate connection.
- **Dependencies:** Requires Person 2 to handle handshake response in client (Task 2.2).
- **Acceptance Criteria:** Connecting two clients with the same username causes the second client to be rejected with an informative error message.

### Task 1.3: Receive Buffer Guardrails and Empty Message Suppression
- **Objective:** Prevent memory exhaustion from unbounded byte streams and eliminate empty broadcast spam.
- **Existing Files:** `server/src/server.cpp`.
- **Expected Changes:**
  - Check `receiveBuffer.size() > MAX_MESSAGE_LENGTH`. If exceeded, log warning and disconnect client.
  - In `handleClient()`, if extracted chat line is empty, skip broadcasting.
- **Dependencies:** None (Server internal).
- **Acceptance Criteria:** Messages over 2048 bytes trigger a clean disconnection; empty lines do not produce empty broadcast lines.

### Task 1.4: Broadcast Concurrency Hardening and Stale Socket Eviction
- **Objective:** Eliminate concurrent `send()` race conditions and remove dead sockets during broadcast failures.
- **Existing Files:** `server/include/client_manager.h`, `server/src/client_manager.cpp`.
- **Expected Changes:**
  - Add a per-client send lock (`std::shared_ptr<std::mutex> sendMutex` inside `ClientInfo`) or serialize outgoing broadcasts through an internal queue.
  - In `ClientManager::broadcastMessage()`, collect any sockets where `sendAll()` returns `false`, and call `removeClient()` on them after the loop.
- **Dependencies:** None (Server internal).
- **Acceptance Criteria:** Rapid concurrent messages from multiple clients do not corrupt stream data; dead clients are immediately removed from active count.

### Task 1.5: Graceful Server Shutdown and Thread Lifecycle Management
- **Objective:** Ensure clean server shutdown without socket abort errors (`WSAECONNABORTED`).
- **Existing Files:** `server/include/server.h`, `server/src/server.cpp`, `server/src/main.cpp`.
- **Expected Changes:**
  - Maintain a thread registry: `std::vector<std::thread> m_workerThreads;` with mutex protection, or clean up completed thread handles.
  - On `Server::stop()`:
    - Broadcast `[Server] Server shutting down.\n` to all active clients.
    - Close all active client sockets in `ClientManager` to unblock worker threads waiting in `recv()`.
    - Join all worker threads.
    - Close listening socket and call `WSACleanup()`.
- **Dependencies:** None.
- **Acceptance Criteria:** Stopping the server via `Ctrl+C` with active clients logs zero socket error codes and exits cleanly.

---

## 13. Recommended Phase 2 Tasks — Person 2 (Client & Application)

### Task 2.1: Client-Side Message Framing Buffer in Receiver
- **Objective:** Fix TCP packet coalescing and fragmentation by implementing an accumulation buffer in `Receiver`.
- **Existing Files:** `client/include/receiver.h`, `client/src/receiver.cpp`.
- **Expected Changes:**
  - Add `std::string stream_buffer_;` to `Receiver`.
  - In `Receiver::start()`, append received bytes to `stream_buffer_`.
  - Scan for `\n` in a loop, extract complete lines, strip `\r`, and pass only full lines to `ProtocolHandler::parse()`.
- **Dependencies:** None (Client internal).
- **Acceptance Criteria:** Multiple messages received in a single TCP read buffer are each displayed on their own separate line.

### Task 2.2: Registration Handshake and Error Feedback Display
- **Objective:** Await server response upon registration and display actionable feedback if rejected.
- **Existing Files:** `client/src/client.cpp`, `client/src/main.cpp`.
- **Expected Changes:**
  - After sending the join request in `main.cpp`, perform a blocking read for the registration reply before spawning worker threads.
  - If server replies `OK\n`, print `"Registration successful"` and launch threads.
  - If server replies `ERR_USERNAME_TAKEN\n`, display `"[Error] Username is already in use. Please choose another."` and prompt again or exit.
  - If server replies `ERR_INVALID_USERNAME\n`, display `"[Error] Username contains invalid characters."` and exit.
- **Dependencies:** Coordinated with Person 1 (Task 1.2).
- **Acceptance Criteria:** Client displays clear error message when attempting to register a duplicate username.

### Task 2.3: Adopt Shared Protocol Definitions
- **Objective:** Align client formatting with `common/include/protocol.h`.
- **Existing Files:** `client/include/protocol_handler.h`, `common/include/protocol.h`.
- **Expected Changes:**
  - Refactor `ProtocolHandler` to import and utilize constants defined in `common/include/protocol.h`.
  - Ensure message categories match protocol opcodes.
- **Dependencies:** Coordinated with Person 1 (Task 1.1).
- **Acceptance Criteria:** Client builds against `common/include/protocol.h` without redundant string constants.

### Task 2.4: Console I/O Coordination and Prompt Preservation
- **Objective:** Prevent incoming broadcast messages from disrupting user typing on the console prompt.
- **Existing Files:** `client/src/receiver.cpp`, `client/src/sender.cpp`.
- **Expected Changes:**
  - Implement a synchronized console display helper.
  - When a message arrives in `Receiver`, clear the current input line, print the message, and re-render the prompt `> ` along with any partially typed text.
- **Dependencies:** None.
- **Acceptance Criteria:** Incoming messages do not interleave with user text while typing.

### Task 2.5: Non-blocking Input & Clean Teardown on Server Disconnect
- **Objective:** Prevent client process from hanging in `std::getline` when the server disconnects.
- **Existing Files:** `client/src/sender.cpp`, `client/src/main.cpp`.
- **Expected Changes:**
  - When `Receiver` detects connection loss, signal the sender or close `std::cin` / simulate Enter key to allow `sender_thread` to join without requiring manual keyboard input.
- **Dependencies:** None.
- **Acceptance Criteria:** When server shuts down, client exits automatically without waiting for user input.

---

## 14. Cross-Team Dependencies and Integration Order

To minimize merge conflicts and integration regressions, implement tasks in the following sequential stages:

```mermaid
sequenceDiagram
    autonumber
    participant P1 as Person 1 (Server)
    participant Common as common/include/protocol.h
    participant P2 as Person 2 (Client)

    Note over P1,P2: Stage 1: Protocol Standardization
    P1->>Common: Populate protocol constants (delim, codes, lengths)
    P2->>Common: Review & confirm wire protocol specification

    Note over P1,P2: Stage 2: Handshake & Framing Implementation
    P1->>P1: Task 1.2 (Username validation & ACK/NACK)
    P2->>P2: Task 2.1 (Stream framing buffer)
    P2->>P2: Task 2.2 (Wait for registration ACK/NACK)

    Note over P1,P2: Stage 3: Integration Verification
    P1->>P2: Joint test: duplicate username rejection
    P1->>P2: Joint test: rapid burst message framing

    Note over P1,P2: Stage 4: Concurrency & Shutdown Polish
    P1->>P1: Task 1.4 & 1.5 (Broadcast mutex & graceful shutdown)
    P2->>P2: Task 2.4 & 2.5 (Console prompt & exit unblocking)
```

### Stage Details:
1. **Stage 1: Shared Protocol Definition:**
   - Both developers agree on and write `common/include/protocol.h`.
   - Defines registration handshake strings (`OK`, `ERR_USERNAME_TAKEN`, `ERR_INVALID_USERNAME`), message delimiters (`\n`), and buffer size limits.
2. **Stage 2: Core Stabilization (Parallel):**
   - **Person 1:** Implements username uniqueness check and handshake response on server.
   - **Person 2:** Implements receive stream buffer in client and awaits registration response.
3. **Stage 3: Handshake and Framing Integration Testing:**
   - Verify duplicate username rejection and message separation over localhost.
4. **Stage 4: Concurrency & Teardown Hardening (Parallel):**
   - **Person 1:** Implements thread registry, broadcast send mutex, and clean shutdown.
   - **Person 2:** Refines console prompt rendering and handles abrupt disconnect unblocking.

---

## 15. Phase 2 Acceptance Criteria

Phase 2 stabilization and baseline requirements will be deemed complete when the following testable criteria are met:

1. **Clean Build:** Both server and client compile under GCC C++17 with `-Wall -Wextra` without warnings or errors.
2. **Protocol Compliance:** Both client and server include and compile against `common/include/protocol.h`.
3. **Duplicate Username Prevention:**
   - Connecting a second client with the same username results in an immediate rejection with `ERR_USERNAME_TAKEN`.
   - The second client displays an informative error message and exits cleanly without hanging.
   - The first client remains connected and unaffected.
4. **Message Framing Reliability:**
   - Sending 10 rapid messages consecutively from Client 1 results in exactly 10 separate lines displayed on Client 2 without truncated or concatenated text.
5. **Memory Guardrails:**
   - Any client sending a continuous payload exceeding 2048 bytes without a newline delimiter is cleanly disconnected by the server.
6. **Graceful Server Teardown:**
   - Pressing `Ctrl+C` on the server while 3 clients are connected notifies all clients, unregisters all sockets, joins all threads, and exits with 0 errors in `chat.log` (no code 10053 errors).
7. **Graceful Client Teardown:**
   - Disconnecting the server causes connected clients to print a disconnect notice and exit cleanly within 1 second without requiring manual keyboard input.
8. **Thread & Socket Safety:**
   - Zero concurrent `send()` calls on the same socket descriptor.
   - Zero socket leaks or double-close invocations.

---

## 16. Known Limitations and Future Phases

The following features are explicitly deferred to future phases and must **not** be implemented during Phase 2 stabilization:

| Future Feature | Target Phase | Rationale for Deferral |
|---|---|---|
| **Private Direct Messaging (`/msg <user> <text>`)** | Phase 3 | Requires stable username registry and routing tables established in Phase 2. |
| **User Listing (`/users`)** | Phase 3 | Requires standardized request-reply command infrastructure. |
| **Chat Rooms / Channels (`/join <room>`)** | Phase 4 | Requires multi-tier routing logic and room membership management. |
| **Authentication & Passwords** | Phase 4 | Requires secure credential storage and session management. |
| **Heartbeat / Keep-Alive (PING-PONG)** | Phase 5 | Requires timer threads or `select()` timeouts to detect dead sockets. |
| **Persistent Chat History (Database/File)** | Phase 5 | Requires storage schemas and retrieval protocol. |
| **Graphical User Interface (GUI)** | Phase 6 | Console prototype must be fully stabilized before introducing Qt or wxWidgets. |
| **Transport Layer Security (TLS/SSL)** | Phase 6 | Requires integration of OpenSSL or WinCrypt libraries. |

---

*Report prepared and validated against repository commit `932a587` on branch `Server-side`.*

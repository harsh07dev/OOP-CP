# WebSocket-to-TCP Bridge for C++ Multi-Client Chat System

This service provides a high-performance, transparent networking bridge that connects browser-based WebSocket clients directly to the existing C++17 TCP Chat Server on port `8080`.

---

## 1. Architecture & Session Model

```text
┌─────────────────┐       WebSocket        ┌───────────────────────┐         TCP (Raw Stream)        ┌─────────────────┐
│ Browser Client  │ ─────────────────────► │   WebSocket Bridge    │ ──────────────────────────────► │ C++ TCP Server  │
│ (e.g. Alice)    │ ◄───────────────────── │      (Port 8081)      │ ◄────────────────────────────── │   (Port 8080)   │
└─────────────────┘                        └───────────────────────┘                                 └─────────────────┘
                                                       ▲
                                                       │ 1:1 Mapping per connection
                                                       ▼
┌─────────────────┐       WebSocket        ┌───────────────────────┐         TCP (Raw Stream)        ┌─────────────────┐
│ Browser Client  │ ─────────────────────► │   WebSocket Bridge    │ ──────────────────────────────► │ C++ TCP Server  │
│ (e.g. Bob)      │ ◄───────────────────── │  (Dedicated net.Socket│ ◄────────────────────────────── │   (Port 8080)   │
└─────────────────┘                        └───────────────────────┘                                 └─────────────────┘
```

### Key Invariants
1. **1:1 Stateful Session Mapping**:
   * Every browser WebSocket connection initiates an independent `net.Socket` connection to the C++ server.
   * Session identity is **not** created by spoofed client fields; it is rooted directly in the distinct TCP connection descriptor managed by the C++ server's `ClientManager`.
2. **Transparent Protocol Passthrough**:
   * The bridge **never fabricates** delivery ACKs, message IDs, or presence events. All status acknowledgements (`msg_ack`, `msg_direct_ack`, `room_msg_ack`) and error codes flow directly from the C++ backend.
3. **Framing & Wire Bounding**:
   * Outgoing frames are validated against `MAX_MESSAGE_SIZE` (2048 bytes), framed with `\n` (`protocol::DELIMITER`), and transmitted over TCP.
   * Incoming TCP streams are buffered and split on `\n` boundaries, with individual JSON lines forwarded as discrete WebSocket text messages to the browser.
4. **Lifecycle Synchronization**:
   * When a browser tab closes or disconnects, the bridge closes the underlying TCP socket immediately, triggering the C++ server's graceful client removal, room cleanup, and `system_event`/`user_left` broadcast.

---

## 2. Configuration & Environment Variables

The bridge can be configured using environment variables or command-line flags:

| Setting | Env Variable | CLI Flag | Default | Description |
|---|---|---|---|---|
| **Bridge Host** | `BRIDGE_HOST` | `--host` | `0.0.0.0` | Host interface the WebSocket server binds to. |
| **Bridge Port** | `BRIDGE_PORT` / `PORT` | `--port` | `8081` | Port the WebSocket server listens on. |
| **TCP Host** | `TCP_HOST` | `--tcp-host` | `127.0.0.1` | Hostname or IP of the C++ TCP chat server. |
| **TCP Port** | `TCP_PORT` | `--tcp-port` | `8080` | Port of the C++ TCP chat server. |
| **Max Payload** | `MAX_PAYLOAD_SIZE` | *(none)* | `2048` | Maximum permissible wire frame size in bytes. |
| **Heartbeat** | `PING_INTERVAL_MS` | *(none)* | `30000` | Periodic WebSocket ping interval in milliseconds. |

---

## 3. Running Locally

### Prerequisites
* Node.js v18+ (tested on Node v24)
* Compiled C++ server executable (`build/chat_server.exe` on Windows or `build/chat_server` on Linux)

### Steps

1. **Start the C++ TCP Server (Terminal 1)**:
   ```bash
   # From repository root
   ./build/chat_server.exe 8080 chat_history.db
   ```

2. **Start the WebSocket Bridge (Terminal 2)**:
   ```bash
   cd bridge
   npm install       # (First time only)
   npm run dev       # Starts using tsx
   ```
   *Or for production compilation*:
   ```bash
   npm run build
   npm start
   ```

3. **Verify Bridge Health**:
   Open a browser or curl:
   ```bash
   curl http://localhost:8081/health
   # Returns: {"status":"ok","activeSessions":0,"tcpTarget":"127.0.0.1:8080",...}
   ```

---

## 4. Exposing on a Trusted Local Network (Demo Setup)

To allow demo participants on the same Wi-Fi / Local Area Network (LAN) to connect via their browsers:

1. **Configure Bridge Host**:
   By default, `BRIDGE_HOST` binds to `0.0.0.0` (all network interfaces). You can make it explicit:
   ```bash
   # In bridge/
   BRIDGE_HOST=0.0.0.0 BRIDGE_PORT=8081 TCP_HOST=127.0.0.1 TCP_PORT=8080 npm start
   ```

2. **Find Your Local IP Address**:
   * **Windows**: Run `ipconfig` in PowerShell and check your `IPv4 Address` (e.g., `192.168.1.50`).
   * **Linux/macOS**: Run `ip addr` or `ifconfig` (e.g., `192.168.1.50`).

3. **Allow Inbound Firewall Access (Windows Example)**:
   Ensure incoming connections on port `8081` are permitted:
   ```powershell
   New-NetFirewallRule -DisplayName "TCP Chat WebSocket Bridge" -Direction Inbound -LocalPort 8081 -Protocol TCP -Action Allow
   ```

4. **Client Connection URL**:
   Browser clients on the same network connect to:
   ```text
   ws://192.168.1.50:8081
   ```

---

## 5. Running Integration Tests

The integration test suite starts an instance of `chat_server.exe` and verifies multi-session identity isolation, ACKs, direct messages, rooms, presence, and oversized payload defense:

```bash
cd bridge
npm run test
```

# Protocol Specification & Compatibility Audit (Phase 4 — Task 1)

**Document:** `docs/PROTOCOL_SPECIFICATION.md`  
**Status:** Implemented and Tested (28/28 Regression Scenarios Passing)  
**Applicability:** C++17 Multi-Client TCP Chat Architecture  

---

## 1. Overview and Architecture

This document defines the communication protocol for the C++17 TCP Multi-Client Chat Application.
The architecture supports two distinct operational modes over a persistent stream socket:
1. **Legacy Protocol (v0)**: Plain text, newline-delimited commands developed in Phase 1–3.
2. **Structured Protocol (v1)**: Self-describing, single-line JSON objects with newline-delimited framing, correlation IDs, timestamps, monotonic message sequencing, and reserved attributes for roadmap expansion (private messages, chat rooms, message persistence, and session recovery).

Both protocols operate over standard IPv4 TCP stream sockets with strict 2048-byte wire framing.

---

## 2. Wire Framing and Invariants

| Attribute | Specification | Rationale |
|---|---|---|
| **Framing Delimiter** | Newline byte `\n` (`0x0A`) | Stream boundary for TCP framing. Carriage returns (`\r`) before delimiters are stripped. |
| **Max Message Size** | 2048 bytes (including `\n`) | Hard upper limit to prevent buffer exhaustion and denial of service. |
| **Encoding** | UTF-8 | Universal unicode character representation. |
| **Multiline Content** | JSON-escaped `\n` (`\x5C\x6E`) | Enables multi-line chat content without breaking the outer TCP framing boundary. |
| **Concurrency** | Per-client send synchronization | Dedicated `std::mutex` per client ensures zero interleaving of concurrent broadcasts. |

---

## 3. Protocol Version Negotiation & Dual-Mode Compatibility

Rather than naive string matching, the server employs an unambiguous protocol detection heuristic (`isAttemptedStructured`) upon the very first line received over a connection:

```
                      +-----------------------------+
                      |   Client TCP Connection     |
                      +-----------------------------+
                                     |
                                     v
                      +-----------------------------+
                      |   First Received Line       |
                      +-----------------------------+
                                     |
                    +----------------+----------------+
                    |                                 |
         isAttemptedStructured() == true    isAttemptedStructured() == false
                    |                                 |
                    v                                 v
        [ Structured Protocol v1 ]         [ Legacy Protocol v0 ]
        - Strips UTF-8 BOM & whitespace    - Validated as raw username
        - Validates JSON envelope          - Rejects any '{' or symbols
        - Malformed -> ERR_MALFORMED_MSG   - Sends OK or ERR_...
        - NEVER falls through to legacy    - Plain text broadcasts
```

### Protocol Detection Rules (`isAttemptedStructured`):
1. **BOM & Whitespace Normalization**: Strips optional UTF-8 BOM (`\xEF\xBB\xBF`) and leading whitespace (`' '`, `'\t'`, `'\r'`).
2. **Opening Brace Heuristic**: Any line whose first non-whitespace character is `{` is identified as structured JSON intent.
3. **Structured Marker Detection**: Any line containing JSON structured keys (e.g. `"version"`, `"type"`, `register_req`, `msg_send`) is identified as structured protocol.
4. **Strict Isolation**: Once identified as an attempted structured message, the connection is locked to Structured Protocol mode. If deserialization fails or fields are invalid, the server emits a structured JSON error (`ERR_MALFORMED_MESSAGE`) and disconnects; it **never** falls through to legacy username evaluation.
5. **Mathematical Impossibility of Legacy Misinterpretation**: Legacy usernames require `[a-zA-Z0-9_-]` (1 to 32 chars). Any JSON syntax containing `{`, `}`, `"`, `:`, `,`, ` `, or `\` automatically fails legacy username validation.

### Wire Size Invariants Under JSON Escaping
JSON string escaping expands certain characters (`"` -> `\"`, `\` -> `\\`, `\n` -> `\n`, control codes -> `\u00xx`). Consequently, a raw unescaped message under 2048 bytes can expand beyond the 2048-byte wire limit once wrapped in the serialized JSON broadcast envelope (`msg_broadcast`).
- **Server Rejection (`msg_send`)**: If `structBroadcast.serializeWire().size() > 2048`, the server rejects the message with `ERR_MESSAGE_TOO_LARGE` (returning the sender's request ID `id`), drops the broadcast, and does not leak oversized frames onto the network.
- **Defensive Bounding (`broadcastStructured`)**: All broadcast pathways invoke `truncateToFitWire(2048)` to guarantee that no serialized JSON envelope ever exceeds the 2048-byte wire framing limit, preserving UTF-8 boundary integrity.

---

## 4. Message Schema (Structured Protocol v1)

All structured messages contain:
- `"version"`: Integer (must be `1` for Phase 4).
- `"type"`: String identifying the message contract.

### 4.1 Client Registration Request (`register_req`)
Sent as the initial handshake packet:
```json
{"version": 1, "type": "register_req", "id": "req-101", "username": "Alice"}
```
**Validation Rules:**
- `username`: 1 to 32 characters, matching `[a-zA-Z0-9_-]`. No spaces, newlines, or control characters.
- Duplicate usernames are rejected case-insensitively.

### 4.2 Server Registration Acknowledgement (`register_ack`)
Sent by server upon successful registration:
```json
{"version": 1, "type": "register_ack", "id": "req-101", "status": "ok", "username": "Alice"}
```

### 4.3 Client Message Send (`msg_send`)
Sent by a registered client to broadcast or route a chat message:
```json
{"version": 1, "type": "msg_send", "id": "req-102", "content": "Hello everyone!", "to": "", "room": ""}
```
*Note: `"to"` and `"room"` are reserved roadmap attributes (empty for global broadcast).*

### 4.4 Server Message Acknowledgement (`msg_ack`)
Sent to the sender acknowledging acceptance and assigning a persistent message ID:
```json
{"version": 1, "type": "msg_ack", "id": "req-102", "status": "ok", "msg_id": "msg-1"}
```

### 4.5 Server Broadcast Event (`msg_broadcast`)
Broadcast to other connected clients:
```json
{
  "version": 1,
  "type": "msg_broadcast",
  "msg_id": "msg-1",
  "from": "Alice",
  "content": "Hello everyone!",
  "timestamp": 1729800000,
  "to": "",
  "room": ""
}
```

### 4.6 Server System Event (`system_event`)
Notifies clients of joins, leaves, or administrative announcements:
```json
{
  "version": 1,
  "type": "system_event",
  "event": "join",
  "username": "Alice",
  "detail": "[System] Alice has joined the chat.",
  "timestamp": 1729800000
}
```

### 4.7 Client Disconnection Request (`disconnect_req`)
Sent by client prior to terminating socket:
```json
{"version": 1, "type": "disconnect_req", "id": "req-103"}
```

### 4.8 Structured Error Response (`error`)
Returned when a request fails validation:
```json
{
  "version": 1,
  "type": "error",
  "id": "req-101",
  "status": "error",
  "code": "ERR_USERNAME_TAKEN",
  "detail": "Username 'Alice' is already taken."
}
```

---

## 5. Standard Error Codes

| Error Code | Trigger Condition | Worker Thread Action |
|---|---|---|
| `ERR_MALFORMED_MESSAGE` | Non-JSON text from structured client, syntax error, or unclosed string | Sends error; continues or terminates based on severity |
| `ERR_UNSUPPORTED_VERSION` | `"version"` is not equal to `1` | Sends error; safely closes connection |
| `ERR_UNKNOWN_TYPE` | Unrecognized `"type"` identifier | Sends error; discards unknown message |
| `ERR_INVALID_USERNAME` | Username fails character or length validation rules | Sends error; closes connection |
| `ERR_USERNAME_TAKEN` | Username matches existing client case-insensitively | Sends error; closes connection |
| `ERR_EMPTY_MESSAGE` | `"content"` string is empty | Sends error; ignores message |
| `ERR_MESSAGE_TOO_LARGE` | Wire size exceeds 2048 bytes | Sends error; drops frame or terminates rogue stream |

---

## 6. Implementation Architecture

1. **[common/include/json_mini.h](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/common/include/json_mini.h)**:
   - Zero-dependency, header-only C++17 JSON parser and serializer.
   - Deterministic key ordering via `std::map`.
   - String escaping and unescaping (`\n`, `\r`, `\t`, `\"`, `\\`, unicode hex).
2. **[common/include/protocol.h](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/common/include/protocol.h)**:
   - Centralizes version constants, message types, status tokens, and username validation.
3. **[common/include/protocol_message.h](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/common/include/protocol_message.h)**:
   - High-level `StructuredMessage` representation, serialization/deserialization, semantic validation, and factory helpers.
4. **[server/include/client_manager.h](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/include/client_manager.h) & [server/src/client_manager.cpp](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/client_manager.cpp)**:
   - Tracks client protocol version in `ClientInfo`.
   - Implements `broadcastStructured()` delivering format-appropriate payloads to legacy and structured clients.
5. **[server/src/server.cpp](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp)**:
   - Evaluates connection handshake and establishes client protocol mode.
   - Enforces structured error responses and monotonic message sequencing.

---

## 7. Test Results (28/28 Passing)

The complete regression suite in [tests/regression_tests.py](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/tests/regression_tests.py) executes both Phase 2/3 legacy tests and Phase 4 structured tests:

| Test ID | Test Name | Protocol Tested | Status |
|---|---|---|---|
| 1 | Server and client compile cleanly | Both | **Passed** |
| 2 | Two or three clients connect simultaneously | Legacy v0 | **Passed** |
| 3 | Bidirectional broadcasting still works | Legacy v0 | **Passed** |
| 4 | The sender does not receive its own broadcast | Legacy v0 | **Passed** |
| 5 | Duplicate usernames are rejected case-insensitively | Legacy v0 | **Passed** |
| 6 | Invalid usernames are rejected | Legacy v0 | **Passed** |
| 7 | Valid messages at the size limit | Legacy v0 | **Passed** |
| 8 | Oversized messages are rejected safely | Legacy v0 | **Passed** |
| 9 | Fragmented and coalesced newline-delimited input | Legacy v0 | **Passed** |
| 10 | Abrupt client disconnection removes the client | Legacy v0 | **Passed** |
| 11 | Failed broadcast does not terminate unrelated clients | Legacy v0 | **Passed** |
| 12 | Concurrent broadcasts do not interleave message frames | Legacy v0 | **Passed** |
| 13 | Server shutdown while clients are connected completes cleanly | Legacy v0 | **Passed** |
| 14 | Client shutdown and /quit work without hanging | Legacy v0 | **Passed** |
| 15 | The server can restart and accept new connections | Legacy v0 | **Passed** |
| 16 | Logs remain timestamped and append correctly | Both | **Passed** |
| 17 | Registration handshake remains compatible with client | Legacy v0 | **Passed** |
| 18 | Structured client registration and ACK | Structured v1 | **Passed** |
| 19 | Structured client duplicate username rejection | Structured v1 | **Passed** |
| 20 | Structured chat broadcast and message ACK with correlation IDs | Structured v1 | **Passed** |
| 21 | Cross-protocol bidirectional broadcasting (Structured <-> Legacy) | Dual-mode | **Passed** |
| 22 | Escaped newlines in message content without breaking framing | Structured v1 | **Passed** |
| 23 | Malformed structured messages rejected safely | Structured v1 | **Passed** |
| 24 | Unsupported protocol version rejected safely | Structured v1 | **Passed** |
| 25 | Missing required fields and empty content rejected safely | Structured v1 | **Passed** |
| 26 | Structured disconnect request handled and broadcast correctly | Dual-mode | **Passed** |
| 27 | Protocol detection heuristic & structured message isolation | Structured v1 | **Passed** |
| 28 | Message-size constraint under JSON escaping and wire bounding | Dual-mode | **Passed** |

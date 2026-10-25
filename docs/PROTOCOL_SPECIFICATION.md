# Protocol Specification & Compatibility Audit (Phase 4 — Task 4)

**Document:** `docs/PROTOCOL_SPECIFICATION.md`  
**Status:** Implemented and Tested (56/56 Regression Scenarios Passing)
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

### 4.9 Online User List Request (`user_list_req`)
Sent by a registered structured client to request the current active online users:
```json
{"version": 1, "type": "user_list_req", "id": "req-list-1", "page": 1, "limit": 50}
```
**Fields:**
- `"id"`: Optional client-assigned correlation ID.
- `"page"`: Optional 1-indexed page number (default `1`). Negative values are rejected.
- `"limit"`: Optional page size (default `50`, clamped to maximum `50`).

### 4.10 Online User List Response (`user_list_resp`)
Returned by the server containing active registered users:
```json
{
  "version": 1,
  "type": "user_list_resp",
  "id": "req-list-1",
  "status": "ok",
  "users": ["Alice", "Bob", "Charlie"],
  "count": 3,
  "total_users": 3,
  "page": 1,
  "total_pages": 1
}
```
**Semantics and Wire Size Policy:**
- Includes all currently active, successfully registered users (including the requesting client).
- Duplicate and rejected registrations never appear in the list.
- Disconnected clients are removed immediately before leave events are dispatched.
- Page limit is strictly clamped to `50` users. With maximum username length of 32 characters, $50 \times 35 + 130 \approx 1880$ bytes, guaranteeing every page response fits within the 2048-byte wire frame.
- If a response would exceed 2048 bytes, the server safely responds with `ERR_MESSAGE_TOO_LARGE` without leaking oversized frames or truncating usernames.

### 4.11 User Join and Leave Updates (`user_joined`, `user_left`, `system_event`)
When a user registers or disconnects, the server broadcasts an event to all remaining clients:
- **Join Event (`system_event` with `event: "join"` or `user_joined`)**:
```json
{
  "version": 1,
  "type": "system_event",
  "event": "join",
  "username": "David",
  "detail": "[System] David has joined the chat.",
  "timestamp": 1729800000
}
```
- **Leave Event (`system_event` with `event: "leave"` or `user_left`)**:
```json
{
  "version": 1,
  "type": "system_event",
  "event": "leave",
  "username": "David",
  "detail": "[System] David has left the chat.",
  "timestamp": 1729800000
}
```
The server guarantees that exactly one leave notification is dispatched when a client disconnects.

### 4.12 Private Messaging (`msg_direct_req`, `msg_direct_ack`, `msg_direct`)
A registered structured client may send private messages directly to another connected user without leaking content to public chat or unrelated peers.

#### Request Schema (`msg_direct_req`)
```json
{
  "version": 1,
  "type": "msg_direct_req",
  "id": "req-dm-1",
  "to": "Bob",
  "content": "Hello Bob, this is a secret!"
}
```
- `"to"`: Recipient username (case-insensitive lookup, 1–32 characters, `[a-zA-Z0-9_-]`).
- `"content"`: Message text (1 to max wire bytes, non-empty).
- `"id"`: Client correlation ID.

#### Acknowledgement Schema (`msg_direct_ack`)
Sent to the sender acknowledging server acceptance and socket dispatch:
```json
{
  "version": 1,
  "type": "msg_direct_ack",
  "id": "req-dm-1",
  "status": "ok",
  "msg_id": "msg-4",
  "to": "Bob"
}
```
**Acknowledgement Semantics:**
`msg_direct_ack` confirms that the server has validated the request, resolved the recipient, assigned a monotonic `msg_id`, and successfully queued/transmitted the message payload to the recipient socket. It **does not** imply that the recipient has read, rendered, or acknowledged the message on their client UI (read receipts are reserved for future protocol revisions).

#### Recipient Event Schema (`msg_direct`)
Delivered strictly and exclusively to the recipient socket:
```json
{
  "version": 1,
  "type": "msg_direct",
  "msg_id": "msg-4",
  "from": "Alice",
  "content": "Hello Bob, this is a secret!",
  "timestamp": 1729800005
}
```

#### Privacy and Routing Invariants:
1. **Zero Public Leakage**: Private messages are never broadcast to the general channel or to legacy clients.
2. **Sender Exclusion**: The sender does not receive `msg_direct`; they receive the correlated `msg_direct_ack`.
3. **Privacy-Preserving Logs**: The server logger records metadata (`msg_id`, sender, recipient, byte length) but **never logs plaintext message content**.
4. **Error Handling**:
   - Nonexistent recipient -> `ERR_RECIPIENT_NOT_FOUND`.
   - Attempting to direct-message oneself -> `ERR_CANNOT_MESSAGE_SELF`.
   - Oversized message (envelope > 2048 bytes) -> `ERR_MESSAGE_TOO_LARGE`.
   - Recipient disconnects during delivery -> gracefully handled without crashing worker threads or leaking to others.

---

### 4.13 Chat Rooms
Chat rooms provide distinct, isolated multi-user channels within the server.

#### Room Policies:
1. **Naming Rules**: Room names must be 1 to 32 characters, containing only alphanumeric characters, underscores, and hyphens (`[a-zA-Z0-9_-]`).
2. **Case-Insensitivity & Uniqueness**: Room names are unique case-insensitively (`#General` and `#general` are the exact same room). Room identifiers are stored and matched in normalized lowercase, while preserving display name conventions.
3. **Creator Auto-Join**: The user who creates a room automatically joins as its initial member.
4. **Empty Room Lifecycle**: Rooms are strictly ephemeral and in-memory. When the last member leaves (via explicit `room_leave_req` or disconnect), the room is automatically deleted. Re-creating the room thereafter is permitted.
5. **Sender ACK Policy**: When a member sends `room_msg_req`, the sender receives `room_msg_ack` containing `msg_id` and does not receive the peer broadcast `room_msg`.
6. **Authorization Enforcement**: Non-members cannot send messages to a room or query room members (`ERR_NOT_IN_ROOM`). Unregistered clients cannot perform any room operations (`ERR_NOT_REGISTERED`).
7. **Pagination**: Room lists and member lists support pagination (`page` and `limit`, capped at 50) ensuring all responses fit within 2048 bytes.

#### Room Creation (`room_create_req` & `room_create_resp`)
```json
{"version": 1, "type": "room_create_req", "id": "req-cr-1", "room": "gaming"}
```
Response:
```json
{"version": 1, "type": "room_create_resp", "id": "req-cr-1", "status": "ok", "room": "gaming"}
```
Errors: `ERR_INVALID_ROOM_NAME`, `ERR_ROOM_ALREADY_EXISTS`.

#### Room Joining (`room_join_req` & `room_join_resp`)
```json
{"version": 1, "type": "room_join_req", "id": "req-jn-1", "room": "gaming"}
```
Response:
```json
{"version": 1, "type": "room_join_resp", "id": "req-jn-1", "status": "ok", "room": "gaming"}
```
Errors: `ERR_ROOM_NOT_FOUND`, `ERR_ALREADY_IN_ROOM`.

#### Room Membership Notifications (`room_user_joined` & `room_user_left`)
When a user joins, existing members receive:
```json
{"version": 1, "type": "room_user_joined", "room": "gaming", "username": "Bob", "timestamp": 1729800010}
```
When a user leaves or disconnects, remaining members receive:
```json
{"version": 1, "type": "room_user_left", "room": "gaming", "username": "Bob", "timestamp": 1729800020}
```

#### Room Leaving (`room_leave_req` & `room_leave_resp`)
```json
{"version": 1, "type": "room_leave_req", "id": "req-lv-1", "room": "gaming"}
```
Response:
```json
{"version": 1, "type": "room_leave_resp", "id": "req-lv-1", "status": "ok", "room": "gaming"}
```
Errors: `ERR_ROOM_NOT_FOUND`, `ERR_NOT_IN_ROOM`.

#### Room Messaging (`room_msg_req`, `room_msg_ack`, `room_msg`)
Request:
```json
{"version": 1, "type": "room_msg_req", "id": "req-rm-1", "room": "gaming", "content": "GG everyone!"}
```
Acknowledgement (to sender):
```json
{"version": 1, "type": "room_msg_ack", "id": "req-rm-1", "status": "ok", "msg_id": "msg-8", "room": "gaming"}
```
Broadcast Event (to other members of the room):
```json
{
  "version": 1,
  "type": "room_msg",
  "msg_id": "msg-8",
  "room": "gaming",
  "from": "Alice",
  "content": "GG everyone!",
  "timestamp": 1729800025
}
```

#### Room Listing (`room_list_req` & `room_list_resp`)
Request:
```json
{"version": 1, "type": "room_list_req", "id": "req-rl-1", "page": 1, "limit": 50}
```
Response:
```json
{
  "version": 1,
  "type": "room_list_resp",
  "id": "req-rl-1",
  "status": "ok",
  "rooms": ["gaming", "general", "lobby"],
  "count": 3,
  "total_rooms": 3,
  "page": 1,
  "total_pages": 1
}
```

#### Room Members Listing (`room_members_req` & `room_members_resp`)
Request (caller must be a current room member):
```json
{"version": 1, "type": "room_members_req", "id": "req-rm-1", "room": "gaming", "page": 1, "limit": 50}
```
Response:
```json
{
  "version": 1,
  "type": "room_members_resp",
  "id": "req-rm-1",
  "status": "ok",
  "room": "gaming",
  "members": ["Alice", "Bob"],
  "count": 2,
  "total_members": 2,
  "page": 1,
  "total_pages": 1
}
```

---

### 4.14 Persistent Message History

SQLite-backed persistent message history supports durable storage and authorized, paginated, cursor-based retrieval of public chat, direct messages, and room conversations.

#### 1. Storage Architecture & Schema:
Storage is implemented via the thread-safe `MessageStore` component using a local SQLite database (`chat_history.db` by default, or configured via CLI argument `chat_server [port] [db_path]`).
- **Schema**:
  ```sql
  CREATE TABLE IF NOT EXISTS messages (
      id INTEGER PRIMARY KEY,
      msg_id TEXT UNIQUE NOT NULL,
      conv_type TEXT NOT NULL,
      sender TEXT NOT NULL,
      recipient TEXT,
      direct_pair TEXT,
      room_name TEXT,
      room_instance_id INTEGER,
      content TEXT NOT NULL,
      created_at INTEGER NOT NULL
  );
  CREATE INDEX IF NOT EXISTS idx_messages_public ON messages(conv_type, id ASC);
  CREATE INDEX IF NOT EXISTS idx_messages_direct ON messages(conv_type, direct_pair, id ASC);
  CREATE INDEX IF NOT EXISTS idx_messages_room ON messages(conv_type, room_instance_id, id ASC);
  ```
- **Monotonic Sequence Resumption**: Upon startup, `MessageStore` initializes its sequence counter from `SELECT COALESCE(MAX(id), 0) FROM messages`, ensuring `msg-<id>` values resume without collision across restarts.
- **Direct Message Pair Normalization**: Usernames are canonicalized into sorted lowercase pairs (`"alice:bob"`) stored in `direct_pair`, allowing indexed $O(\log N)$ bi-directional lookups.
- **Room Instance Isolation**: Each room creation generates a distinct `room_instance_id` (resumed from `MAX(room_instance_id)`). When an ephemeral room is deleted and later re-created with the same name, historical messages remain associated with the original `room_instance_id`, preventing historical leakage to the new room incarnation.
- **Commit-Before-ACK Semantics**: A success acknowledgement (`msg_ack`, `msg_direct_ack`, `room_msg_ack`) and broadcast are only issued after a successful SQLite transaction commit. Storage failures immediately abort the broadcast and return `ERR_STORAGE_ERROR`.
- **Disconnect Resilience**: Once committed to SQLite, subsequent socket delivery failures do not delete or roll back the message.

#### 2. Authorization Rules:
1. **Public History**: Accessible to any registered client.
2. **Direct History**: Accessible only to the two participants (`user == sender || user == recipient`). Third parties cannot retrieve or enumerate messages from other conversations. Attempting to query history with oneself is rejected with `ERR_CANNOT_MESSAGE_SELF`.
3. **Room History**: Accessible only to active members currently joined to that room instance (`ERR_NOT_IN_ROOM` if not in the room or after leaving).

#### 3. Pagination & Wire Framing Bounding:
- **Parameters**: `limit` (default 20, maximum 50).
- **Cursor Mechanism**:
  - `before_id`: Returns messages strictly older than the cursor ID (backward pagination).
  - `after_id`: Returns messages strictly newer than the cursor ID (forward pagination).
  - No cursor: Returns the most recent page of messages up to `limit`.
- **Wire Bounding Invariant**: Complete `HistoryItem` records are packed dynamically into `history_resp` until the 2048-byte `MAX_MESSAGE_SIZE` frame limit is approached. When packing would exceed 2048 bytes, the server halts packing, sets `has_more: true`, and transmits the valid JSON frame without truncation.

#### History Request (`history_req`)
```json
{"version": 1, "type": "history_req", "id": "req-h-1", "conversation_type": "direct", "with": "Bob", "limit": 20, "before_id": "msg-45"}
```
Fields:
- `conversation_type` (required): `"public"`, `"direct"`, or `"room"`.
- `with` (required for direct): Participant username.
- `room` (required for room): Room name.
- `limit` (optional): Items per page (default 20, max 50).
- `before_id` (optional): Cursor for backward pagination.
- `after_id` (optional): Cursor for forward pagination.

#### History Response (`history_resp`)
```json
{
  "version": 1,
  "type": "history_resp",
  "id": "req-h-1",
  "status": "ok",
  "conversation_type": "direct",
  "with": "Bob",
  "count": 2,
  "has_more": false,
  "messages": [
    {
      "id": "msg-40",
      "msg_id": "msg-40",
      "type": "direct",
      "from": "Alice",
      "to": "Bob",
      "content": "Hello Bob!",
      "timestamp": 1729800030
    },
    {
      "id": "msg-41",
      "msg_id": "msg-41",
      "type": "direct",
      "from": "Bob",
      "to": "Alice",
      "content": "Hi Alice!",
      "timestamp": 1729800035
    }
  ]
}
```

---

## 5. Standard Error Codes

| Error Code | Trigger Condition | Worker Thread Action |
|---|---|---|
| `ERR_MALFORMED_MESSAGE` | Non-JSON text from structured client, syntax error, or unclosed string | Sends error; continues or terminates based on severity |
| `ERR_UNSUPPORTED_VERSION` | `"version"` is not equal to `1` | Sends error; safely closes connection |
| `ERR_UNKNOWN_TYPE` | Unrecognized `"type"` identifier | Sends error; discards unknown message |
| `ERR_NOT_REGISTERED` | Client sent room/DM/list/history request before registering | Sends error; closes connection |
| `ERR_INVALID_PAGE` | Page number in query is negative | Sends error; ignores request |
| `ERR_INVALID_USERNAME` | Username fails character or length validation rules | Sends error; closes connection |
| `ERR_USERNAME_TAKEN` | Username matches existing client case-insensitively | Sends error; closes connection |
| `ERR_EMPTY_MESSAGE` | `"content"` string is empty | Sends error; ignores message |
| `ERR_MESSAGE_TOO_LARGE` | Wire size exceeds 2048 bytes | Sends error; drops frame or terminates rogue stream |
| `ERR_RECIPIENT_NOT_FOUND` | Target recipient username does not exist or is offline | Sends error with correlation ID |
| `ERR_CANNOT_MESSAGE_SELF` | Target recipient username matches sender | Sends error with correlation ID |
| `ERR_INVALID_ROOM_NAME` | Room name empty, > 32 characters, or contains invalid characters | Sends error with correlation ID |
| `ERR_ROOM_ALREADY_EXISTS` | Room name already exists (case-insensitive) | Sends error with correlation ID |
| `ERR_ROOM_NOT_FOUND` | Room name does not exist | Sends error with correlation ID |
| `ERR_NOT_IN_ROOM` | Client attempts to send message, query history, or list members of a room they have not joined | Sends error with correlation ID |
| `ERR_ALREADY_IN_ROOM` | Client attempts to join a room they are already in | Sends error with correlation ID |
| `ERR_STORAGE_ERROR` | SQLite database execution, connection, or commit error | Sends error with correlation ID; prevents false ACK/broadcast |
| `ERR_UNAUTHORIZED` | Client lacks authorization to access requested resource | Sends error with correlation ID |
| `ERR_INVALID_CONVERSATION` | Invalid conversation type or missing required conversation target | Sends error with correlation ID |

---

## 6. Implementation Architecture

1. **[common/include/json_mini.h](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/common/include/json_mini.h)**:
   - Zero-dependency, header-only C++17 JSON parser and serializer.
   - Deterministic key ordering via `std::map`.
   - String escaping and unescaping (`\n`, `\r`, `\t`, `\"`, `\\`, unicode hex).
2. **[common/include/protocol.h](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/common/include/protocol.h)**:
   - Centralizes version constants, message types, status tokens, pagination limits, room limits, and validation helpers.
3. **[common/include/protocol_message.h](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/common/include/protocol_message.h)**:
   - High-level `StructuredMessage` representation, serialization/deserialization, semantic validation, pagination helpers, and factory helpers for all private, room, and history message contracts.
4. **[server/include/message_store.h](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/include/message_store.h) & [server/src/message_store.cpp](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/message_store.cpp)**:
   - Thread-safe persistence component managing an embedded SQLite database.
   - Automatic schema initialization, WAL journaling, prepared statement caching, and monotonic ID resumption.
   - Lock isolation: database I/O is performed strictly under `MessageStore::m_mutex`, completely decoupling database locks from `ClientManager` or `RoomManager` mutexes.
5. **[server/include/client_manager.h](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/include/client_manager.h) & [server/src/client_manager.cpp](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/client_manager.cpp)**:
   - Tracks client protocol version and metadata in `ClientInfo`.
   - Thread-safe `getClientByUsername()` performs case-insensitive recipient resolution.
   - Snapshot-under-mutex design: socket transmission occurs strictly after releasing manager mutexes.
6. **[server/include/room_manager.h](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/include/room_manager.h) & [server/src/room_manager.cpp](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/room_manager.cpp)**:
   - Thread-safe `RoomManager` manages room lifecycles and memberships under `m_mutex`.
   - Tracks unique monotonically increasing `instanceId` per room incarnation to guarantee historical message isolation upon room name re-use.
   - Independent mutex hierarchy: `RoomManager` never acquires `ClientManager` locks and vice versa.
7. **[server/src/server.cpp](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/server/src/server.cpp)**:
   - Handshake detection, registration enforcement, monotonic message sequencing, private routing, room operations, history pagination, and clean disconnect teardown.
8. **[client/include/chat_session.h](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/include/chat_session.h) & [client/src/chat_session.cpp](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/src/chat_session.cpp)**:
   - Structured registration handshake, console command parsing, request correlation IDs, and human-readable rendering of all server event types.
9. **[client/include/console_input.h](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/client/include/console_input.h)**:
   - Cross-platform non-blocking stdin readiness check so the sender can exit promptly when the server disconnects.

---

## 7. Test Results (56/56 Passing)

The complete regression suite in [tests/regression_tests.py](file:///c:/Users/harsh/OneDrive/Desktop/OOP-CP/tests/regression_tests.py) executes legacy tests, online users list tests, private messaging and room tests, SQLite persistence history tests, and interactive console-client command tests:

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
| 29 | Online user list query, correlation IDs, and registry integrity | Structured v1 | **Passed** |
| 30 | Real-time join/leave updates and registry synchronization | Dual-mode | **Passed** |
| 31 | User list pagination and oversized wire limit policy | Structured v1 | **Passed** |
| 32 | Concurrent registration, query, and disconnection thread-safety | Structured v1 | **Passed** |
| 33 | Private messaging delivery, correlation ID, and isolation | Structured v1 | **Passed** |
| 34 | Private messaging recipient resolution and error handling | Structured v1 | **Passed** |
| 35 | Private messaging wire limit bounding and privacy preservation | Structured v1 | **Passed** |
| 36 | Chat room lifecycle (create, duplicate rejection, join, leave, empty auto-delete) | Structured v1 | **Passed** |
| 37 | Chat room messaging, sender ACK policy, and member isolation | Structured v1 | **Passed** |
| 38 | Chat room authorization and non-member rejection | Structured v1 | **Passed** |
| 39 | Chat room membership events and disconnect cleanup | Structured v1 | **Passed** |
| 40 | Chat room listing, member listing, and pagination | Structured v1 | **Passed** |
| 41 | Chat room wire limit bounding | Structured v1 | **Passed** |
| 42 | Concurrent room operations, private messaging, and thread safety | Structured v1 | **Passed** |
| 43 | Cross-protocol isolation (legacy client compatibility) | Dual-mode | **Passed** |
| 44 | Public history retrieval, ordering, and empty history | Structured v1 | **Passed** |
| 45 | Direct message persistence, bi-directional retrieval, and third-party isolation | Structured v1 | **Passed** |
| 46 | Room message persistence, membership authorization, and non-member isolation | Structured v1 | **Passed** |
| 47 | Persistence across server restart and durable message ID uniqueness | Structured v1 | **Passed** |
| 48 | Cursor-based pagination (before_id and after_id) | Structured v1 | **Passed** |
| 49 | Parameter validation and authorization bounds | Structured v1 | **Passed** |
| 50 | Escaped characters and Unicode round-tripping | Structured v1 | **Passed** |
| 51 | Oversized history wire limit bounding | Structured v1 | **Passed** |
| 52 | Room name reuse historical isolation | Structured v1 | **Passed** |
| 53 | Storage failure handling and zero false ACK/broadcast | Structured v1 | **Passed** |
| 54 | Concurrent public, direct, and room message storage integrity | Structured v1 | **Passed** |
| 55 | Disconnect resilience (delivery failure after DB commit does not erase message) | Structured v1 | **Passed** |
| 56 | Interactive console client commands (public chat, users, DMs, rooms, history, `/quit`) | Structured v1 | **Passed** |

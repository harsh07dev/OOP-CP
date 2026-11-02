/**
 * types.ts — Shared TypeScript types for the ChatDesk frontend.
 * Mirrors the exact v1 protocol defined in docs/PROTOCOL_SPECIFICATION.md.
 */

/* ── Connection state ────────────────────────────────────────────── */
export type ConnectionStatus = 'disconnected' | 'connecting' | 'connected' | 'error';

/* ── Message delivery states ─────────────────────────────────────── */
// pending   → sent to bridge, awaiting server response
// accepted  → server returned msg_ack / room_msg_ack (stored in SQLite, broadcast to others)
// dispatched→ server returned msg_direct_ack (stored in SQLite, dispatched to recipient socket)
// failed    → server returned error OR WS closed while pending
// (no "delivered" or "read" state — the server does not provide end-to-end read receipts)
export type MessageStatus = 'pending' | 'accepted' | 'dispatched' | 'failed';

/* ── Persistence confirmation states ────────────────────────────── */
// Backend commit-before-ACK invariant:
// ACK is issued ONLY after SQLite commit. Therefore ACK = confirmed storage.
export type PersistenceStatus = 'confirmed' | 'pending' | 'failed' | 'not_applicable';

/* ── Message kinds ───────────────────────────────────────────────── */
export type MessageKind = 'public' | 'private' | 'room' | 'system' | 'error';

/* ── Chat message (rendered UI) ──────────────────────────────────── */
export interface ChatMessage {
  id: string;                 // Local ephemeral UI ID
  msgId?: string;             // Server-assigned msg_id (from SQLite / ACK)
  correlationId?: string;     // Client request correlation ID ('id' field)
  protocolVersion?: number;   // 1 for v1 structured JSON
  rawType?: string;           // 'msg_send', 'msg_broadcast', 'msg_direct_req', etc.
  kind: MessageKind;
  sender: string;
  body: string;
  timestamp: Date;
  own?: boolean;              // true if this client sent it
  pending?: boolean;          // awaiting server ACK
  status?: MessageStatus;
  persistence?: PersistenceStatus;
  target?: string;            // for private: recipient username
  room?: string;              // for room messages
  rawPayload?: string;        // Exact JSON frame for Flight Recorder / Lens
}

/* ── History entry (from history_resp) ──────────────────────────── */
export interface HistoryEntry {
  id: string;
  msg_id: string;
  type: string;               // "public" | "direct" | "room"
  from: string;
  to?: string;
  content: string;
  timestamp: number;          // Unix seconds
  room?: string;
}

/* ── Protocol lens frame ─────────────────────────────────────────── */
export interface ProtocolFrame {
  id: string;
  timestamp: Date;
  direction: 'in' | 'out';
  rawText: string;
  wireBytes: number;
  parsed?: Record<string, unknown>;
  protocolVersion: 'v1-structured' | 'v0-legacy' | 'unknown';
  type?: string;
  correlationId?: string;
  summary: string;
}

/* ── Presence event ──────────────────────────────────────────────── */
export interface PresenceEvent {
  id: string;
  timestamp: Date;
  type: 'join' | 'leave';
  username: string;
  detail: string;
}

/* ── Room lifecycle event ────────────────────────────────────────── */
export interface RoomLifecycleEvent {
  id: string;
  timestamp: Date;
  type: 'user_joined' | 'user_left' | 'created';
  room: string;
  username: string;
}

/* ── Chat context (which conversation is focused) ────────────────── */
export type ChatContext =
  | { type: 'global' }
  | { type: 'room'; room: string }
  | { type: 'dm'; peer: string };

/* ── Navigation views ────────────────────────────────────────────── */
export type NavView = 'chats' | 'people' | 'rooms' | 'history' | 'lab';

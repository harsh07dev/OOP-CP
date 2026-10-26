/**
 * types.ts — Shared TypeScript types for the ChatDesk frontend.
 * Mirrors the exact v1 protocol defined in docs/PROTOCOL_SPECIFICATION.md.
 */

/* ── Connection state ────────────────────────────────────────────── */
export type ConnectionStatus = 'disconnected' | 'connecting' | 'connected' | 'error';

/* ── Message delivery states ─────────────────────────────────────── */
// pending   → sent to bridge, awaiting server response
// accepted  → server returned msg_ack / room_msg_ack (stored, broadcast to others)
// dispatched→ server returned msg_direct_ack (validated + dispatched to recipient socket)
// failed    → server returned error OR WS closed while pending
// (no "delivered" state — the server does not provide end-to-end delivery receipts)
export type MessageStatus = 'pending' | 'accepted' | 'dispatched' | 'failed';

/* ── Message kinds ───────────────────────────────────────────────── */
export type MessageKind = 'public' | 'private' | 'room' | 'system' | 'error';

/* ── Chat message (rendered UI) ──────────────────────────────────── */
export interface ChatMessage {
  id: string;           // Local ephemeral ID (never shown to user)
  msgId?: string;       // Server-assigned msg_id (from ACK)
  kind: MessageKind;
  sender: string;
  body: string;
  timestamp: Date;
  own?: boolean;        // true if this client sent it
  pending?: boolean;    // awaiting server ACK
  status?: MessageStatus;
  target?: string;      // for private: recipient username
  room?: string;        // for room messages
}

/* ── History entry (from history_resp) ──────────────────────────── */
export interface HistoryEntry {
  id: string;
  msg_id: string;
  type: string;           // "public" | "direct" | "room"
  from: string;
  to?: string;
  content: string;
  timestamp: number;      // Unix seconds
  room?: string;
}

/* ── Chat context (which conversation is focused) ────────────────── */
export type ChatContext =
  | { type: 'global' }
  | { type: 'room'; room: string }
  | { type: 'dm'; peer: string };

/* ── Navigation views ────────────────────────────────────────────── */
export type NavView = 'chats' | 'people' | 'rooms' | 'history' | 'lab';

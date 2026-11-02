/**
 * useChatSocket.ts — WebSocket hook implementing the exact v1 JSON protocol
 * as defined in docs/PROTOCOL_SPECIFICATION.md and common/include/protocol.h.
 *
 * Implements:
 * 1. People: Online users registry, presence events (join/leave) tracking.
 * 2. Rooms: Discovered room listing, join/leave lifecycle, room members listing, room events.
 * 3. History: Cursor-based pagination (before_id, after_id), authorization enforcement.
 * 4. Flight Recorder: Message metadata tracking (correlation ID, server msg_id, protocol version,
 *    ACK status, persistence state, and exact raw payload).
 * 5. Protocol Lens: Real-time inbound/outbound wire frame capture with redaction support.
 * 6. System Lab: Real connection metrics (uptime, inbound/outbound frame and byte counters).
 */

import { useCallback, useEffect, useRef, useState } from 'react';
import type {
  ChatMessage,
  ConnectionStatus,
  HistoryEntry,
  ProtocolFrame,
  PresenceEvent,
  RoomLifecycleEvent,
} from '../types';

// ── Wire-size limit (2047 bytes of payload + 1 byte '\n') ──────────────────
const MAX_WIRE_BYTES = 2047;

// ── Counter for correlation IDs ────────────────────────────────────────────
let _reqCounter = 0;
const makeId = () => `fe-${++_reqCounter}-${Date.now()}`;

// ── Server message shape (union of all known server→client types) ──────────
interface ServerFrame {
  version?: number;
  type?: string;
  // Correlation
  id?: string;
  // Status
  status?: 'ok' | 'error';
  // register_ack
  username?: string;
  // msg_ack, msg_direct_ack, room_msg_ack
  msg_id?: string;
  to?: string;
  room?: string;
  // msg_broadcast, msg_direct, room_msg
  from?: string;
  content?: string;
  timestamp?: number;
  // system_event
  event?: string; // "join" | "leave"
  detail?: string;
  // user_list_resp
  users?: string[];
  count?: number;
  total_users?: number;
  page?: number;
  total_pages?: number;
  // room_list_resp
  rooms?: string[];
  total_rooms?: number;
  // room_members_resp
  members?: string[];
  total_members?: number;
  // history_resp
  messages?: HistoryEntry[];
  has_more?: boolean;
  conversation_type?: string;
  with?: string;
  // error
  code?: string;
}

// ── Hook API ───────────────────────────────────────────────────────────────
export interface ChatSocketAPI {
  status: ConnectionStatus;
  username: string | null;
  onlineUsers: string[];
  totalUsers: number;
  presenceEvents: PresenceEvent[];

  // Rooms
  joinedRooms: string[];
  discoveredRooms: string[];
  roomMembers: Record<string, string[]>;
  roomEvents: RoomLifecycleEvent[];
  roomError: string | null;
  totalRooms: number;
  roomListPage: number;
  roomListTotalPages: number;

  // Messaging & History
  messages: ChatMessage[];
  historyEntries: HistoryEntry[];
  historyHasMore: boolean;
  historyCount: number;
  historyError: string | null;

  // Pagination for user list
  userListPage: number;
  userListTotalPages: number;

  // Protocol Lens & System Lab Stats
  protocolFrames: ProtocolFrame[];
  connectedAt: Date | null;
  inboundCount: number;
  outboundCount: number;
  inboundBytes: number;
  outboundBytes: number;

  // Connection management
  connect(bridgeUrl: string, loginAs: string): void;
  disconnect(): void;

  // Messaging
  sendPublic(content: string): void;
  sendPrivate(to: string, content: string): void;

  // Rooms
  createRoom(room: string): void;
  joinRoom(room: string): void;
  leaveRoom(room: string): void;
  sendRoomMessage(room: string, content: string): void;
  requestRoomList(page?: number, limit?: number): void;
  requestRoomMembers(room: string, page?: number, limit?: number): void;
  clearRoomError(): void;

  // Queries
  requestUserList(page?: number, limit?: number): void;
  requestHistory(
    conversationType: 'public' | 'direct' | 'room',
    opts?: {
      with?: string;
      room?: string;
      limit?: number;
      before_id?: string;
      after_id?: string;
    }
  ): void;
  clearHistoryError(): void;

  // Diagnostics & Lab
  clearMessages(): void;
  clearProtocolFrames(): void;
  sendRawFrame(raw: string): boolean;
}

export function useChatSocket(): ChatSocketAPI {
  const [status, setStatus] = useState<ConnectionStatus>('disconnected');
  const [username, setUsername] = useState<string | null>(null);
  const [onlineUsers, setOnlineUsers] = useState<string[]>([]);
  const [totalUsers, setTotalUsers] = useState<number>(0);
  const [presenceEvents, setPresenceEvents] = useState<PresenceEvent[]>([]);

  // Rooms state
  const [joinedRooms, setJoinedRooms] = useState<string[]>([]);
  const [discoveredRooms, setDiscoveredRooms] = useState<string[]>([]);
  const [roomMembers, setRoomMembers] = useState<Record<string, string[]>>({});
  const [roomEvents, setRoomEvents] = useState<RoomLifecycleEvent[]>([]);
  const [roomError, setRoomError] = useState<string | null>(null);
  const [totalRooms, setTotalRooms] = useState<number>(0);
  const [roomListPage, setRoomListPage] = useState<number>(1);
  const [roomListTotalPages, setRoomListTotalPages] = useState<number>(1);

  // Messaging state
  const [messages, setMessages] = useState<ChatMessage[]>([]);
  const [historyEntries, setHistoryEntries] = useState<HistoryEntry[]>([]);
  const [historyHasMore, setHistoryHasMore] = useState<boolean>(false);
  const [historyCount, setHistoryCount] = useState<number>(0);
  const [historyError, setHistoryError] = useState<string | null>(null);

  // User list pagination
  const [userListPage, setUserListPage] = useState<number>(1);
  const [userListTotalPages, setUserListTotalPages] = useState<number>(1);

  // Protocol Lens & System Lab metrics
  const [protocolFrames, setProtocolFrames] = useState<ProtocolFrame[]>([]);
  const [connectedAt, setConnectedAt] = useState<Date | null>(null);
  const [inboundCount, setInboundCount] = useState<number>(0);
  const [outboundCount, setOutboundCount] = useState<number>(0);
  const [inboundBytes, setInboundBytes] = useState<number>(0);
  const [outboundBytes, setOutboundBytes] = useState<number>(0);

  const wsRef = useRef<WebSocket | null>(null);
  const usernameRef = useRef<string | null>(null);
  const pendingLoginRef = useRef<string | null>(null);

  // Map from correlation id → local message id (for ACK tracking)
  const pendingAcks = useRef(new Map<string, string>());
  // Map from correlation id → request type for error routing
  const pendingReqTypes = useRef(new Map<string, string>());

  // Track server msg_ids we have received to prevent duplicates on reconnect
  const seenMsgIds = useRef(new Set<string>());

  // Keep username ref current
  useEffect(() => { usernameRef.current = username; }, [username]);

  // ── Record a wire frame in the Protocol Lens buffer ───────────────────────
  const recordFrame = useCallback((
    direction: 'in' | 'out',
    rawText: string,
    bytes: number
  ) => {
    let parsed: Record<string, unknown> | undefined;
    let protoVersion: 'v1-structured' | 'v0-legacy' | 'unknown' = 'unknown';
    let type: string | undefined;
    let correlationId: string | undefined;
    let summary = rawText.slice(0, 80);

    try {
      parsed = JSON.parse(rawText) as Record<string, unknown>;
      protoVersion = parsed.version === 1 ? 'v1-structured' : 'unknown';
      type = typeof parsed.type === 'string' ? parsed.type : undefined;
      correlationId = typeof parsed.id === 'string' ? parsed.id : undefined;

      if (type) {
        summary = `${direction === 'in' ? '←' : '→'} ${type}${correlationId ? ` [${correlationId}]` : ''}`;
        if (parsed.from) summary += ` from:${parsed.from}`;
        if (parsed.to) summary += ` to:${parsed.to}`;
        if (parsed.room) summary += ` #${parsed.room}`;
      }
    } catch {
      // Legacy v0 plain text or unparsed
      protoVersion = 'v0-legacy';
      summary = `${direction === 'in' ? '←' : '→'} (v0) ${rawText.trim()}`;
    }

    const frame: ProtocolFrame = {
      id: `frame-${Date.now()}-${Math.random().toString(36).slice(2, 7)}`,
      timestamp: new Date(),
      direction,
      rawText,
      wireBytes: bytes,
      parsed,
      protocolVersion: protoVersion,
      type,
      correlationId,
      summary,
    };

    setProtocolFrames(prev => [frame, ...prev.slice(0, 199)]);
  }, []);

  // ── Message factory ───────────────────────────────────────────────────
  const appendMessage = useCallback(
    (msg: Omit<ChatMessage, 'id' | 'timestamp'> & { id?: string; timestamp?: Date }): string => {
      const id = msg.id ?? `local-${Date.now()}-${Math.random().toString(36).slice(2)}`;
      const full: ChatMessage = {
        ...msg,
        id,
        timestamp: msg.timestamp ?? new Date(),
        protocolVersion: msg.protocolVersion ?? 1,
      };
      setMessages(prev => [...prev.slice(-800), full]);
      return id;
    },
    []
  );

  // ── Send a raw JSON frame to the server ───────────────────────────────
  const sendFrame = useCallback((obj: Record<string, unknown>): boolean => {
    const ws = wsRef.current;
    if (!ws || ws.readyState !== WebSocket.OPEN) return false;
    const json = JSON.stringify(obj);
    const wireBytes = new TextEncoder().encode(json).length + 1; // +1 for '\n'
    if (wireBytes > MAX_WIRE_BYTES) {
      appendMessage({
        kind: 'error',
        sender: 'client',
        body: `Message too large (${wireBytes} bytes > ${MAX_WIRE_BYTES} limit). Shorten your message.`,
        own: false,
        protocolVersion: 1,
        persistence: 'failed',
      });
      return false;
    }

    ws.send(json);
    setOutboundCount(prev => prev + 1);
    setOutboundBytes(prev => prev + wireBytes);
    recordFrame('out', json, wireBytes);
    return true;
  }, [appendMessage, recordFrame]);

  // ── Handle incoming server frame ──────────────────────────────────────
  const handleFrame = useCallback((raw: string) => {
    const bytes = new TextEncoder().encode(raw).length;
    setInboundCount(prev => prev + 1);
    setInboundBytes(prev => prev + bytes);
    recordFrame('in', raw, bytes);

    let frame: ServerFrame;
    try {
      frame = JSON.parse(raw) as ServerFrame;
    } catch {
      appendMessage({
        kind: 'error',
        sender: 'bridge',
        body: `Malformed frame (not JSON): ${raw.slice(0, 60)}`,
        own: false,
        rawPayload: raw,
        persistence: 'not_applicable',
      });
      return;
    }

    const { type, id: corrId } = frame;

    switch (type) {
      // ── Registration ──────────────────────────────────────────────────
      case 'register_ack': {
        if (frame.status === 'ok' && frame.username) {
          const uname = frame.username;
          setUsername(uname);
          usernameRef.current = uname;
          appendMessage({
            kind: 'system',
            sender: 'system',
            body: `Signed in as ${uname}`,
            rawPayload: raw,
            rawType: 'register_ack',
            correlationId: corrId,
            persistence: 'not_applicable',
          });
          // Fetch initial online users and rooms
          sendFrame({ version: 1, type: 'user_list_req', id: makeId(), page: 1, limit: 50 });
          sendFrame({ version: 1, type: 'room_list_req', id: makeId(), page: 1, limit: 50 });
        }
        break;
      }

      // ── Public broadcast (received by others, NOT the sender) ─────────
      case 'msg_broadcast': {
        const msgId = frame.msg_id;
        if (msgId && seenMsgIds.current.has(msgId)) break; // dedup
        if (msgId) seenMsgIds.current.add(msgId);
        appendMessage({
          kind: 'public',
          sender: frame.from ?? '?',
          body: frame.content ?? '',
          msgId,
          own: false,
          rawType: 'msg_broadcast',
          rawPayload: raw,
          persistence: 'confirmed', // Already committed to SQLite before server broadcast
          status: 'accepted',
        });
        break;
      }

      // ── Server accepted your public message ───────────────────────────
      // Commit-before-ACK semantics: SQLite commit succeeded!
      case 'msg_ack': {
        const localId = corrId ? pendingAcks.current.get(corrId) : undefined;
        if (localId) {
          pendingAcks.current.delete(corrId!);
          const serverMsgId = frame.msg_id;
          if (serverMsgId) seenMsgIds.current.add(serverMsgId);
          setMessages(prev =>
            prev.map(m =>
              m.id === localId
                ? {
                    ...m,
                    msgId: serverMsgId,
                    status: 'accepted',
                    persistence: 'confirmed',
                    pending: false,
                    rawPayload: raw,
                  }
                : m
            )
          );
        }
        break;
      }

      // ── Incoming private message (you are recipient) ──────────────────
      case 'msg_direct': {
        const msgId = frame.msg_id;
        if (msgId && seenMsgIds.current.has(msgId)) break;
        if (msgId) seenMsgIds.current.add(msgId);
        appendMessage({
          kind: 'private',
          sender: frame.from ?? '?',
          body: frame.content ?? '',
          msgId,
          own: false,
          rawType: 'msg_direct',
          rawPayload: raw,
          persistence: 'confirmed',
          status: 'dispatched',
        });
        break;
      }

      // ── Server accepted + dispatched your private message ─────────────
      // msg_direct_ack confirms stored in SQLite and dispatched to recipient socket
      case 'msg_direct_ack': {
        const localId = corrId ? pendingAcks.current.get(corrId) : undefined;
        if (localId) {
          pendingAcks.current.delete(corrId!);
          const serverMsgId = frame.msg_id;
          if (serverMsgId) seenMsgIds.current.add(serverMsgId);
          setMessages(prev =>
            prev.map(m =>
              m.id === localId
                ? {
                    ...m,
                    msgId: serverMsgId,
                    status: 'dispatched',
                    persistence: 'confirmed',
                    pending: false,
                    rawPayload: raw,
                  }
                : m
            )
          );
        }
        break;
      }

      // ── Room message (received by other members) ──────────────────────
      case 'room_msg': {
        const msgId = frame.msg_id;
        if (msgId && seenMsgIds.current.has(msgId)) break;
        if (msgId) seenMsgIds.current.add(msgId);
        appendMessage({
          kind: 'room',
          sender: frame.from ?? '?',
          body: frame.content ?? '',
          room: frame.room,
          msgId,
          own: false,
          rawType: 'room_msg',
          rawPayload: raw,
          persistence: 'confirmed',
          status: 'accepted',
        });
        break;
      }

      // ── Server accepted your room message ─────────────────────────────
      case 'room_msg_ack': {
        const localId = corrId ? pendingAcks.current.get(corrId) : undefined;
        if (localId) {
          pendingAcks.current.delete(corrId!);
          const serverMsgId = frame.msg_id;
          if (serverMsgId) seenMsgIds.current.add(serverMsgId);
          setMessages(prev =>
            prev.map(m =>
              m.id === localId
                ? {
                    ...m,
                    msgId: serverMsgId,
                    status: 'accepted',
                    persistence: 'confirmed',
                    pending: false,
                    rawPayload: raw,
                  }
                : m
            )
          );
        }
        break;
      }

      // ── System presence events ────────────────────────────────────────
      case 'system_event': {
        const ev = frame.event;
        const uname = frame.username ?? '';
        const detail = frame.detail ?? (ev === 'join' ? `${uname} joined the chat` : `${uname} left the chat`);

        if (ev === 'join' || ev === 'leave') {
          const presEvt: PresenceEvent = {
            id: `pres-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`,
            timestamp: new Date(),
            type: ev,
            username: uname,
            detail,
          };
          setPresenceEvents(prev => [presEvt, ...prev.slice(0, 49)]);

          if (ev === 'join' && uname) {
            setOnlineUsers(prev => (prev.includes(uname) ? prev : [...prev, uname]));
            setTotalUsers(prev => prev + 1);
          } else if (ev === 'leave' && uname) {
            setOnlineUsers(prev => prev.filter(u => u !== uname));
            setTotalUsers(prev => Math.max(0, prev - 1));
          }
        }

        appendMessage({
          kind: 'system',
          sender: 'system',
          body: detail,
          rawType: 'system_event',
          rawPayload: raw,
          persistence: 'not_applicable',
        });
        break;
      }

      // ── Discrete join/left events (protocol compatibility) ────────────
      case 'user_joined': {
        const uname = frame.username ?? '';
        if (uname) {
          const presEvt: PresenceEvent = {
            id: `pres-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`,
            timestamp: new Date(),
            type: 'join',
            username: uname,
            detail: `${uname} joined the chat`,
          };
          setPresenceEvents(prev => [presEvt, ...prev.slice(0, 49)]);
          setOnlineUsers(prev => (prev.includes(uname) ? prev : [...prev, uname]));
          setTotalUsers(prev => prev + 1);
        }
        appendMessage({
          kind: 'system',
          sender: 'system',
          body: `${uname} joined the chat`,
          rawType: 'user_joined',
          rawPayload: raw,
          persistence: 'not_applicable',
        });
        break;
      }

      case 'user_left': {
        const uname = frame.username ?? '';
        if (uname) {
          const presEvt: PresenceEvent = {
            id: `pres-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`,
            timestamp: new Date(),
            type: 'leave',
            username: uname,
            detail: `${uname} left the chat`,
          };
          setPresenceEvents(prev => [presEvt, ...prev.slice(0, 49)]);
          setOnlineUsers(prev => prev.filter(u => u !== uname));
          setTotalUsers(prev => Math.max(0, prev - 1));
        }
        appendMessage({
          kind: 'system',
          sender: 'system',
          body: `${uname} left the chat`,
          rawType: 'user_left',
          rawPayload: raw,
          persistence: 'not_applicable',
        });
        break;
      }

      // ── User list response ────────────────────────────────────────────
      case 'user_list_resp': {
        if (frame.status === 'ok') {
          if (frame.users) setOnlineUsers(frame.users);
          setTotalUsers(frame.total_users ?? frame.count ?? frame.users?.length ?? 0);
          setUserListPage(frame.page ?? 1);
          setUserListTotalPages(frame.total_pages ?? 1);
        }
        break;
      }

      // ── Room responses ────────────────────────────────────────────────
      case 'room_create_resp': {
        if (frame.status === 'ok' && frame.room) {
          const r = frame.room;
          setRoomError(null);
          appendMessage({
            kind: 'system',
            sender: 'system',
            body: `Room #${r} created and joined`,
            rawType: 'room_create_resp',
            rawPayload: raw,
            persistence: 'not_applicable',
          });
          setJoinedRooms(prev => (prev.includes(r) ? prev : [...prev, r]));
          setDiscoveredRooms(prev => (prev.includes(r) ? prev : [...prev, r]));
          const roomEvt: RoomLifecycleEvent = {
            id: `rm-evt-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`,
            timestamp: new Date(),
            type: 'created',
            room: r,
            username: usernameRef.current ?? 'me',
          };
          setRoomEvents(prev => [roomEvt, ...prev.slice(0, 49)]);
        }
        break;
      }

      case 'room_join_resp': {
        if (frame.status === 'ok' && frame.room) {
          const r = frame.room;
          setRoomError(null);
          appendMessage({
            kind: 'system',
            sender: 'system',
            body: `Joined room #${r}`,
            rawType: 'room_join_resp',
            rawPayload: raw,
            persistence: 'not_applicable',
          });
          setJoinedRooms(prev => (prev.includes(r) ? prev : [...prev, r]));
          // Request member list for newly joined room
          const memId = makeId();
          pendingReqTypes.current.set(memId, 'room_members_req');
          sendFrame({ version: 1, type: 'room_members_req', id: memId, room: r, page: 1, limit: 50 });
        }
        break;
      }

      case 'room_leave_resp': {
        if (frame.status === 'ok' && frame.room) {
          const r = frame.room;
          setRoomError(null);
          appendMessage({
            kind: 'system',
            sender: 'system',
            body: `Left room #${r}`,
            rawType: 'room_leave_resp',
            rawPayload: raw,
            persistence: 'not_applicable',
          });
          setJoinedRooms(prev => prev.filter(rm => rm !== r));
          setRoomMembers(prev => {
            const next = { ...prev };
            delete next[r];
            return next;
          });
        }
        break;
      }

      case 'room_list_resp': {
        if (frame.status === 'ok') {
          if (frame.rooms) setDiscoveredRooms(frame.rooms);
          setTotalRooms(frame.total_rooms ?? frame.count ?? frame.rooms?.length ?? 0);
          setRoomListPage(frame.page ?? 1);
          setRoomListTotalPages(frame.total_pages ?? 1);
          setRoomError(null);
        }
        break;
      }

      case 'room_members_resp': {
        if (frame.status === 'ok' && frame.room && frame.members) {
          const r = frame.room;
          const mbrs = frame.members;
          setRoomMembers(prev => ({ ...prev, [r]: mbrs }));
          setRoomError(null);
        }
        break;
      }

      case 'room_user_joined': {
        if (frame.room && frame.username) {
          const r = frame.room;
          const u = frame.username;
          appendMessage({
            kind: 'system',
            sender: 'system',
            body: `${u} joined #${r}`,
            room: r,
            rawType: 'room_user_joined',
            rawPayload: raw,
            persistence: 'not_applicable',
          });
          const roomEvt: RoomLifecycleEvent = {
            id: `rm-evt-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`,
            timestamp: new Date(),
            type: 'user_joined',
            room: r,
            username: u,
          };
          setRoomEvents(prev => [roomEvt, ...prev.slice(0, 49)]);
          setRoomMembers(prev => {
            const existing = prev[r] ?? [];
            if (!existing.includes(u)) {
              return { ...prev, [r]: [...existing, u] };
            }
            return prev;
          });
        }
        break;
      }

      case 'room_user_left': {
        if (frame.room && frame.username) {
          const r = frame.room;
          const u = frame.username;
          appendMessage({
            kind: 'system',
            sender: 'system',
            body: `${u} left #${r}`,
            room: r,
            rawType: 'room_user_left',
            rawPayload: raw,
            persistence: 'not_applicable',
          });
          const roomEvt: RoomLifecycleEvent = {
            id: `rm-evt-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`,
            timestamp: new Date(),
            type: 'user_left',
            room: r,
            username: u,
          };
          setRoomEvents(prev => [roomEvt, ...prev.slice(0, 49)]);
          setRoomMembers(prev => {
            const existing = prev[r];
            if (existing) {
              return { ...prev, [r]: existing.filter(m => m !== u) };
            }
            return prev;
          });
        }
        break;
      }

      // ── History response ──────────────────────────────────────────────
      case 'history_resp': {
        if (frame.status === 'ok') {
          setHistoryEntries(frame.messages ?? []);
          setHistoryHasMore(Boolean(frame.has_more));
          setHistoryCount(frame.count ?? frame.messages?.length ?? 0);
          setHistoryError(null);
        }
        break;
      }

      // ── Error from server ─────────────────────────────────────────────
      case 'error': {
        const failedReq = corrId ? pendingReqTypes.current.get(corrId) : undefined;
        if (corrId) pendingReqTypes.current.delete(corrId);

        const code = frame.code ?? '';
        const detail = frame.detail ?? 'Server error';

        if (failedReq === 'history_req' ||
            code === 'ERR_CANNOT_MESSAGE_SELF' ||
            code === 'ERR_NOT_IN_ROOM' ||
            code === 'ERR_INVALID_CONVERSATION') {
          setHistoryError(`[${code || 'ERR'}] ${detail}`);
        }

        if ((failedReq && failedReq.startsWith('room_')) ||
            code === 'ERR_ROOM_ALREADY_EXISTS' ||
            code === 'ERR_INVALID_ROOM_NAME' ||
            code === 'ERR_ROOM_NOT_FOUND' ||
            code === 'ERR_ALREADY_IN_ROOM' ||
            (code === 'ERR_NOT_IN_ROOM' && failedReq !== 'history_req')) {
          setRoomError(`[${code || 'ERR'}] ${detail}`);
        }

        if (corrId) {
          const localId = pendingAcks.current.get(corrId);
          if (localId) {
            pendingAcks.current.delete(corrId);
            setMessages(prev =>
              prev.map(m =>
                m.id === localId
                  ? { ...m, status: 'failed', persistence: 'failed', pending: false }
                  : m
              )
            );
          }
        }
        appendMessage({
          kind: 'error',
          sender: 'server',
          body: `[${code || 'ERR'}] ${detail}`,
          own: false,
          rawType: 'error',
          correlationId: corrId,
          rawPayload: raw,
          persistence: 'failed',
        });
        break;
      }

      default:
        break;
    }
  }, [appendMessage, recordFrame, sendFrame]);

  // ── Connect ───────────────────────────────────────────────────────────
  const connect = useCallback((bridgeUrl: string, loginAs: string) => {
    if (wsRef.current &&
        (wsRef.current.readyState === WebSocket.OPEN ||
         wsRef.current.readyState === WebSocket.CONNECTING)) {
      return;
    }

    pendingLoginRef.current = loginAs;
    seenMsgIds.current.clear();
    pendingAcks.current.clear();

    setStatus('connecting');
    const ws = new WebSocket(bridgeUrl);
    wsRef.current = ws;

    ws.addEventListener('open', () => {
      setStatus('connected');
      setConnectedAt(new Date());
      appendMessage({ kind: 'system', sender: 'system', body: 'Connected to bridge' });

      const uname = pendingLoginRef.current;
      if (uname) {
        pendingLoginRef.current = null;
        sendFrame({ version: 1, type: 'register_req', id: makeId(), username: uname });
      }
    });

    ws.addEventListener('message', (evt: MessageEvent) => {
      const raw = typeof evt.data === 'string' ? evt.data : String(evt.data);
      handleFrame(raw);
    });

    ws.addEventListener('close', (evt: CloseEvent) => {
      setStatus('disconnected');
      setConnectedAt(null);
      if (pendingAcks.current.size > 0) {
        const failedIds = new Set(pendingAcks.current.values());
        pendingAcks.current.clear();
        setMessages(prev =>
          prev.map(m => (failedIds.has(m.id) ? { ...m, status: 'failed', persistence: 'failed', pending: false } : m))
        );
      }
      setUsername(null);
      usernameRef.current = null;
      appendMessage({
        kind: 'system',
        sender: 'system',
        body: `Disconnected (${evt.code}${evt.reason ? ': ' + evt.reason : ''})`,
      });
      wsRef.current = null;
    });

    ws.addEventListener('error', () => {
      setStatus('error');
      appendMessage({
        kind: 'error',
        sender: 'bridge',
        body: 'WebSocket connection error. Verify bridge is running on the specified URL.',
        own: false,
        persistence: 'failed',
      });
    });
  }, [appendMessage, handleFrame, sendFrame]);

  // ── Disconnect ────────────────────────────────────────────────────────
  const disconnect = useCallback(() => {
    const ws = wsRef.current;
    if (!ws) return;
    if (ws.readyState === WebSocket.OPEN && usernameRef.current) {
      sendFrame({ version: 1, type: 'disconnect_req', id: makeId() });
    }
    setTimeout(() => ws.close(1000, 'User disconnected'), 100);
  }, [sendFrame]);

  // ── Send public message ───────────────────────────────────────────────
  const sendPublic = useCallback((content: string) => {
    const corrId = makeId();
    const payload = { version: 1, type: 'msg_send', id: corrId, content, to: '', room: '' };
    const rawPayload = JSON.stringify(payload);
    const localId = appendMessage({
      kind: 'public',
      sender: usernameRef.current ?? 'me',
      body: content,
      own: true,
      pending: true,
      status: 'pending',
      persistence: 'pending',
      correlationId: corrId,
      rawType: 'msg_send',
      rawPayload,
    });
    pendingAcks.current.set(corrId, localId);
    const sent = sendFrame(payload);
    if (!sent) {
      pendingAcks.current.delete(corrId);
      setMessages(prev =>
        prev.map(m => (m.id === localId ? { ...m, status: 'failed', persistence: 'failed', pending: false } : m))
      );
    }
  }, [appendMessage, sendFrame]);

  // ── Send private message ──────────────────────────────────────────────
  const sendPrivate = useCallback((to: string, content: string) => {
    const corrId = makeId();
    const payload = { version: 1, type: 'msg_direct_req', id: corrId, to, content };
    const rawPayload = JSON.stringify(payload);
    const localId = appendMessage({
      kind: 'private',
      sender: usernameRef.current ?? 'me',
      body: content,
      own: true,
      pending: true,
      status: 'pending',
      persistence: 'pending',
      target: to,
      correlationId: corrId,
      rawType: 'msg_direct_req',
      rawPayload,
    });
    pendingAcks.current.set(corrId, localId);
    const sent = sendFrame(payload);
    if (!sent) {
      pendingAcks.current.delete(corrId);
      setMessages(prev =>
        prev.map(m => (m.id === localId ? { ...m, status: 'failed', persistence: 'failed', pending: false } : m))
      );
    }
  }, [appendMessage, sendFrame]);

  // ── Room operations ───────────────────────────────────────────────────
  const createRoom = useCallback((room: string) => {
    const id = makeId();
    pendingReqTypes.current.set(id, 'room_create_req');
    setRoomError(null);
    sendFrame({ version: 1, type: 'room_create_req', id, room });
  }, [sendFrame]);

  const joinRoom = useCallback((room: string) => {
    const id = makeId();
    pendingReqTypes.current.set(id, 'room_join_req');
    setRoomError(null);
    sendFrame({ version: 1, type: 'room_join_req', id, room });
  }, [sendFrame]);

  const leaveRoom = useCallback((room: string) => {
    const id = makeId();
    pendingReqTypes.current.set(id, 'room_leave_req');
    setRoomError(null);
    sendFrame({ version: 1, type: 'room_leave_req', id, room });
  }, [sendFrame]);

  const sendRoomMessage = useCallback((room: string, content: string) => {
    const corrId = makeId();
    const payload = { version: 1, type: 'room_msg_req', id: corrId, room, content };
    const rawPayload = JSON.stringify(payload);
    const localId = appendMessage({
      kind: 'room',
      sender: usernameRef.current ?? 'me',
      body: content,
      room,
      own: true,
      pending: true,
      status: 'pending',
      persistence: 'pending',
      correlationId: corrId,
      rawType: 'room_msg_req',
      rawPayload,
    });
    pendingAcks.current.set(corrId, localId);
    const sent = sendFrame(payload);
    if (!sent) {
      pendingAcks.current.delete(corrId);
      setMessages(prev =>
        prev.map(m => (m.id === localId ? { ...m, status: 'failed', persistence: 'failed', pending: false } : m))
      );
    }
  }, [appendMessage, sendFrame]);

  const requestRoomList = useCallback((page = 1, limit = 50) => {
    const id = makeId();
    pendingReqTypes.current.set(id, 'room_list_req');
    sendFrame({ version: 1, type: 'room_list_req', id, page, limit });
  }, [sendFrame]);

  const requestRoomMembers = useCallback((room: string, page = 1, limit = 50) => {
    const id = makeId();
    pendingReqTypes.current.set(id, 'room_members_req');
    sendFrame({ version: 1, type: 'room_members_req', id, room, page, limit });
  }, [sendFrame]);

  const clearRoomError = useCallback(() => setRoomError(null), []);

  // ── Queries ───────────────────────────────────────────────────────────
  const requestUserList = useCallback((page = 1, limit = 50) => {
    const id = makeId();
    pendingReqTypes.current.set(id, 'user_list_req');
    sendFrame({ version: 1, type: 'user_list_req', id, page, limit });
  }, [sendFrame]);

  const requestHistory = useCallback((
    conversationType: 'public' | 'direct' | 'room',
    opts?: {
      with?: string;
      room?: string;
      limit?: number;
      before_id?: string;
      after_id?: string;
    }
  ) => {
    const corrId = makeId();
    pendingReqTypes.current.set(corrId, 'history_req');
    setHistoryError(null);
    const payload: Record<string, unknown> = {
      version: 1,
      type: 'history_req',
      id: corrId,
      conversation_type: conversationType,
      limit: opts?.limit ?? 20,
    };
    if (opts?.with) payload.with = opts.with;
    if (opts?.room) payload.room = opts.room;
    if (opts?.before_id) payload.before_id = opts.before_id;
    if (opts?.after_id) payload.after_id = opts.after_id;
    sendFrame(payload);
  }, [sendFrame]);

  const clearHistoryError = useCallback(() => setHistoryError(null), []);

  // ── Raw Frame Sender for Lab / Protocol testing ───────────────────────
  const sendRawFrame = useCallback((raw: string): boolean => {
    const ws = wsRef.current;
    if (!ws || ws.readyState !== WebSocket.OPEN) return false;
    const wireBytes = new TextEncoder().encode(raw).length + 1;
    if (wireBytes > MAX_WIRE_BYTES) {
      appendMessage({
        kind: 'error',
        sender: 'lab',
        body: `Raw frame exceeds wire limit (${wireBytes} > ${MAX_WIRE_BYTES} bytes).`,
        own: false,
        persistence: 'failed',
      });
      return false;
    }
    ws.send(raw);
    setOutboundCount(prev => prev + 1);
    setOutboundBytes(prev => prev + wireBytes);
    recordFrame('out', raw, wireBytes);
    return true;
  }, [appendMessage, recordFrame]);

  const clearMessages = useCallback(() => setMessages([]), []);
  const clearProtocolFrames = useCallback(() => setProtocolFrames([]), []);

  // ── Cleanup on unmount ────────────────────────────────────────────────
  useEffect(() => {
    return () => { wsRef.current?.close(1001, 'Component unmounted'); };
  }, []);

  return {
    status,
    username,
    onlineUsers,
    totalUsers,
    userListPage,
    userListTotalPages,
    presenceEvents,
    joinedRooms,
    discoveredRooms,
    totalRooms,
    roomListPage,
    roomListTotalPages,
    roomMembers,
    roomEvents,
    roomError,
    messages,
    historyEntries,
    historyHasMore,
    historyCount,
    historyError,
    protocolFrames,
    connectedAt,
    inboundCount,
    outboundCount,
    inboundBytes,
    outboundBytes,
    connect,
    disconnect,
    sendPublic,
    sendPrivate,
    createRoom,
    joinRoom,
    leaveRoom,
    sendRoomMessage,
    requestRoomList,
    requestRoomMembers,
    clearRoomError,
    requestUserList,
    requestHistory,
    clearHistoryError,
    clearMessages,
    clearProtocolFrames,
    sendRawFrame,
  };
}

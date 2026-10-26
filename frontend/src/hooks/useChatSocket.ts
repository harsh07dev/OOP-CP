/**
 * useChatSocket.ts — WebSocket hook implementing the exact v1 JSON protocol
 * as defined in docs/PROTOCOL_SPECIFICATION.md and common/include/protocol.h.
 *
 * Protocol correctness notes:
 * - Registration: register_req → register_ack (id field for correlation)
 * - Broadcast:    msg_send → msg_ack (accepted by server) + msg_broadcast (to others)
 * - Private:      msg_direct_req → msg_direct_ack (server dispatched) + msg_direct (to recipient)
 * - msg_ack / msg_direct_ack: confirms server accepted + stored. NOT end-to-end delivery.
 * - system_event{event:"join"|"leave"} is the primary presence notification.
 * - user_joined / user_left are also emitted — both must be handled.
 * - Correlation ID key is "id", NOT "req_id".
 * - Rooms: room_create_req / room_join_req / room_leave_req / room_msg_req
 */

import { useCallback, useEffect, useRef, useState } from 'react';
import type { ChatMessage, ConnectionStatus, HistoryEntry } from '../types';

// ── Wire-size limit (2047 bytes of payload + 1 byte '\n') ──────────────────
const MAX_WIRE_BYTES = 2047;

// ── Counter for correlation IDs ────────────────────────────────────────────
let _reqCounter = 0;
const makeId = () => `fe-${++_reqCounter}-${Date.now()}`;

// ── Server message shape (union of all known server→client types) ──────────
interface ServerFrame {
  version: number;
  type: string;
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
  // room_list_resp
  rooms?: string[];
  // room_members_resp
  members?: string[];
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
  joinedRooms: string[];
  messages: ChatMessage[];
  historyEntries: HistoryEntry[];

  // Connect to bridge WS URL with pending login username
  connect(bridgeUrl: string, loginAs: string): void;
  // Graceful disconnect (sends disconnect_req then closes)
  disconnect(): void;

  // Messaging
  sendPublic(content: string): void;
  sendPrivate(to: string, content: string): void;

  // Rooms
  createRoom(room: string): void;
  joinRoom(room: string): void;
  leaveRoom(room: string): void;
  sendRoomMessage(room: string, content: string): void;

  // Queries
  requestUserList(): void;
  requestRoomList(): void;
  requestHistory(conversationType: 'public' | 'direct' | 'room', opts?: { with?: string; room?: string; limit?: number; before_id?: string }): void;

  clearMessages(): void;
}

export function useChatSocket(): ChatSocketAPI {
  const [status, setStatus] = useState<ConnectionStatus>('disconnected');
  const [username, setUsername] = useState<string | null>(null);
  const [onlineUsers, setOnlineUsers] = useState<string[]>([]);
  const [joinedRooms, setJoinedRooms] = useState<string[]>([]);
  const [messages, setMessages] = useState<ChatMessage[]>([]);
  const [historyEntries, setHistoryEntries] = useState<HistoryEntry[]>([]);

  const wsRef = useRef<WebSocket | null>(null);
  const usernameRef = useRef<string | null>(null);
  const pendingLoginRef = useRef<string | null>(null);

  // Map from correlation id → local message id (for ACK tracking)
  const pendingAcks = useRef(new Map<string, string>());

  // Track server msg_ids we have received to prevent duplicates on reconnect
  const seenMsgIds = useRef(new Set<string>());

  // Keep username ref current without re-creating callbacks
  useEffect(() => { usernameRef.current = username; }, [username]);

  // ── Message factory ───────────────────────────────────────────────────
  const appendMessage = useCallback(
    (msg: Omit<ChatMessage, 'id' | 'timestamp'> & { id?: string; timestamp?: Date }): string => {
      const id = msg.id ?? `local-${Date.now()}-${Math.random().toString(36).slice(2)}`;
      const full: ChatMessage = { ...msg, id, timestamp: msg.timestamp ?? new Date() };
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
      appendMessage({ kind: 'error', sender: 'client', body: `Message too large (${wireBytes} bytes > ${MAX_WIRE_BYTES} limit). Shorten your message.`, own: false });
      return false;
    }
    ws.send(json);
    return true;
  }, [appendMessage]);

  // ── Handle incoming server frame ──────────────────────────────────────
  const handleFrame = useCallback((raw: string) => {
    let frame: ServerFrame;
    try {
      frame = JSON.parse(raw) as ServerFrame;
    } catch {
      appendMessage({ kind: 'error', sender: 'bridge', body: `Malformed frame (not JSON): ${raw.slice(0, 60)}`, own: false });
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
          appendMessage({ kind: 'system', sender: 'system', body: `Signed in as ${uname}` });
          // Fetch online users immediately after registration
          sendFrame({ version: 1, type: 'user_list_req', id: makeId(), page: 1, limit: 50 });
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
        });
        break;
      }

      // ── Server accepted your public message (you sent it) ─────────────
      // msg_ack: server accepted + stored. NOT end-to-end delivery.
      case 'msg_ack': {
        const localId = corrId ? pendingAcks.current.get(corrId) : undefined;
        if (localId) {
          pendingAcks.current.delete(corrId!);
          const serverMsgId = frame.msg_id;
          if (serverMsgId) seenMsgIds.current.add(serverMsgId);
          setMessages(prev =>
            prev.map(m =>
              m.id === localId
                ? { ...m, msgId: serverMsgId, status: 'accepted', pending: false }
                : m
            )
          );
        }
        break;
      }

      // ── Incoming private message (you are the recipient) ──────────────
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
        });
        break;
      }

      // ── Server accepted + dispatched your private message ─────────────
      // msg_direct_ack: confirms server validated, stored, and dispatched to recipient socket.
      // Does NOT confirm recipient read or rendered the message.
      case 'msg_direct_ack': {
        const localId = corrId ? pendingAcks.current.get(corrId) : undefined;
        if (localId) {
          pendingAcks.current.delete(corrId!);
          const serverMsgId = frame.msg_id;
          if (serverMsgId) seenMsgIds.current.add(serverMsgId);
          setMessages(prev =>
            prev.map(m =>
              m.id === localId
                ? { ...m, msgId: serverMsgId, status: 'dispatched', pending: false }
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
                ? { ...m, msgId: serverMsgId, status: 'accepted', pending: false }
                : m
            )
          );
        }
        break;
      }

      // ── System events (join / leave / announcements) ──────────────────
      case 'system_event': {
        const ev = frame.event;
        const uname = frame.username ?? '';
        if (ev === 'join') {
          appendMessage({ kind: 'system', sender: 'system', body: frame.detail ?? `${uname} joined the chat` });
          if (uname) setOnlineUsers(prev => prev.includes(uname) ? prev : [...prev, uname]);
        } else if (ev === 'leave') {
          appendMessage({ kind: 'system', sender: 'system', body: frame.detail ?? `${uname} left the chat` });
          if (uname) setOnlineUsers(prev => prev.filter(u => u !== uname));
        } else {
          appendMessage({ kind: 'system', sender: 'system', body: frame.detail ?? `System: ${ev}` });
        }
        break;
      }

      // ── Also handle discrete join/left events (per protocol.h) ────────
      case 'user_joined': {
        const uname = frame.username ?? '';
        appendMessage({ kind: 'system', sender: 'system', body: `${uname} joined the chat` });
        if (uname) setOnlineUsers(prev => prev.includes(uname) ? prev : [...prev, uname]);
        break;
      }
      case 'user_left': {
        const uname = frame.username ?? '';
        appendMessage({ kind: 'system', sender: 'system', body: `${uname} left the chat` });
        if (uname) setOnlineUsers(prev => prev.filter(u => u !== uname));
        break;
      }

      // ── User list response ────────────────────────────────────────────
      case 'user_list_resp': {
        if (frame.status === 'ok' && frame.users) {
          setOnlineUsers(frame.users);
        }
        break;
      }

      // ── Room responses ────────────────────────────────────────────────
      case 'room_create_resp': {
        if (frame.status === 'ok' && frame.room) {
          appendMessage({ kind: 'system', sender: 'system', body: `Room #${frame.room} created and joined` });
          setJoinedRooms(prev => prev.includes(frame.room!) ? prev : [...prev, frame.room!]);
        }
        break;
      }
      case 'room_join_resp': {
        if (frame.status === 'ok' && frame.room) {
          appendMessage({ kind: 'system', sender: 'system', body: `Joined room #${frame.room}` });
          setJoinedRooms(prev => prev.includes(frame.room!) ? prev : [...prev, frame.room!]);
        }
        break;
      }
      case 'room_leave_resp': {
        if (frame.status === 'ok' && frame.room) {
          appendMessage({ kind: 'system', sender: 'system', body: `Left room #${frame.room}` });
          setJoinedRooms(prev => prev.filter(r => r !== frame.room));
        }
        break;
      }
      case 'room_list_resp': {
        // Room list from server — informational only, doesn't change joinedRooms
        break;
      }
      case 'room_user_joined': {
        if (frame.room && frame.username) {
          appendMessage({ kind: 'system', sender: 'system', body: `${frame.username} joined #${frame.room}`, room: frame.room });
        }
        break;
      }
      case 'room_user_left': {
        if (frame.room && frame.username) {
          appendMessage({ kind: 'system', sender: 'system', body: `${frame.username} left #${frame.room}`, room: frame.room });
        }
        break;
      }

      // ── History response ──────────────────────────────────────────────
      case 'history_resp': {
        if (frame.status === 'ok' && frame.messages) {
          setHistoryEntries(frame.messages);
        }
        break;
      }

      // ── Error from server ─────────────────────────────────────────────
      case 'error': {
        // Resolve pending ACK to 'failed' state if we have correlation
        if (corrId) {
          const localId = pendingAcks.current.get(corrId);
          if (localId) {
            pendingAcks.current.delete(corrId);
            setMessages(prev =>
              prev.map(m =>
                m.id === localId
                  ? { ...m, status: 'failed', pending: false }
                  : m
              )
            );
          }
        }
        appendMessage({
          kind: 'error',
          sender: 'server',
          body: `[${frame.code ?? 'ERR'}] ${frame.detail ?? 'Server error'}`,
          own: false,
        });
        break;
      }

      default:
        // Unknown type — ignore silently per spec
        break;
    }
  }, [appendMessage, sendFrame]);

  // ── Connect ───────────────────────────────────────────────────────────
  const connect = useCallback((bridgeUrl: string, loginAs: string) => {
    // Don't open a second connection
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
      appendMessage({ kind: 'system', sender: 'system', body: 'Connected to bridge' });

      const uname = pendingLoginRef.current;
      if (uname) {
        pendingLoginRef.current = null;
        ws.send(JSON.stringify({ version: 1, type: 'register_req', id: makeId(), username: uname }));
      }
    });

    ws.addEventListener('message', (evt: MessageEvent) => {
      const raw = typeof evt.data === 'string' ? evt.data : String(evt.data);
      handleFrame(raw);
    });

    ws.addEventListener('close', (evt: CloseEvent) => {
      setStatus('disconnected');
      // Mark all still-pending messages as failed
      if (pendingAcks.current.size > 0) {
        const failedIds = new Set(pendingAcks.current.values());
        pendingAcks.current.clear();
        setMessages(prev =>
          prev.map(m => failedIds.has(m.id) ? { ...m, status: 'failed', pending: false } : m)
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
      appendMessage({ kind: 'error', sender: 'bridge', body: 'WebSocket connection error. Check bridge is running on the specified URL.', own: false });
    });
  }, [appendMessage, handleFrame]);

  // ── Disconnect ────────────────────────────────────────────────────────
  const disconnect = useCallback(() => {
    const ws = wsRef.current;
    if (!ws) return;
    // Send graceful disconnect_req if registered
    if (ws.readyState === WebSocket.OPEN && usernameRef.current) {
      ws.send(JSON.stringify({ version: 1, type: 'disconnect_req', id: makeId() }));
    }
    setTimeout(() => ws.close(1000, 'User disconnected'), 100);
  }, []);

  // ── Send public message ───────────────────────────────────────────────
  const sendPublic = useCallback((content: string) => {
    const corrId = makeId();
    const localId = appendMessage({
      kind: 'public',
      sender: usernameRef.current ?? 'me',
      body: content,
      own: true,
      pending: true,
      status: 'pending',
    });
    pendingAcks.current.set(corrId, localId);
    const sent = sendFrame({ version: 1, type: 'msg_send', id: corrId, content, to: '', room: '' });
    if (!sent) {
      pendingAcks.current.delete(corrId);
      setMessages(prev => prev.map(m => m.id === localId ? { ...m, status: 'failed', pending: false } : m));
    }
  }, [appendMessage, sendFrame]);

  // ── Send private message ──────────────────────────────────────────────
  const sendPrivate = useCallback((to: string, content: string) => {
    const corrId = makeId();
    const localId = appendMessage({
      kind: 'private',
      sender: usernameRef.current ?? 'me',
      body: content,
      own: true,
      pending: true,
      status: 'pending',
      target: to,
    });
    pendingAcks.current.set(corrId, localId);
    const sent = sendFrame({ version: 1, type: 'msg_direct_req', id: corrId, to, content });
    if (!sent) {
      pendingAcks.current.delete(corrId);
      setMessages(prev => prev.map(m => m.id === localId ? { ...m, status: 'failed', pending: false } : m));
    }
  }, [appendMessage, sendFrame]);

  // ── Room operations ───────────────────────────────────────────────────
  const createRoom = useCallback((room: string) => {
    sendFrame({ version: 1, type: 'room_create_req', id: makeId(), room });
  }, [sendFrame]);

  const joinRoom = useCallback((room: string) => {
    sendFrame({ version: 1, type: 'room_join_req', id: makeId(), room });
  }, [sendFrame]);

  const leaveRoom = useCallback((room: string) => {
    sendFrame({ version: 1, type: 'room_leave_req', id: makeId(), room });
  }, [sendFrame]);

  const sendRoomMessage = useCallback((room: string, content: string) => {
    const corrId = makeId();
    const localId = appendMessage({
      kind: 'room',
      sender: usernameRef.current ?? 'me',
      body: content,
      room,
      own: true,
      pending: true,
      status: 'pending',
    });
    pendingAcks.current.set(corrId, localId);
    const sent = sendFrame({ version: 1, type: 'room_msg_req', id: corrId, room, content });
    if (!sent) {
      pendingAcks.current.delete(corrId);
      setMessages(prev => prev.map(m => m.id === localId ? { ...m, status: 'failed', pending: false } : m));
    }
  }, [appendMessage, sendFrame]);

  // ── Queries ───────────────────────────────────────────────────────────
  const requestUserList = useCallback(() => {
    sendFrame({ version: 1, type: 'user_list_req', id: makeId(), page: 1, limit: 50 });
  }, [sendFrame]);

  const requestRoomList = useCallback(() => {
    sendFrame({ version: 1, type: 'room_list_req', id: makeId(), page: 1, limit: 50 });
  }, [sendFrame]);

  const requestHistory = useCallback((
    conversationType: 'public' | 'direct' | 'room',
    opts?: { with?: string; room?: string; limit?: number; before_id?: string }
  ) => {
    const payload: Record<string, unknown> = {
      version: 1,
      type: 'history_req',
      id: makeId(),
      conversation_type: conversationType,
      limit: opts?.limit ?? 20,
    };
    if (opts?.with) payload.with = opts.with;
    if (opts?.room) payload.room = opts.room;
    if (opts?.before_id) payload.before_id = opts.before_id;
    sendFrame(payload);
  }, [sendFrame]);

  const clearMessages = useCallback(() => setMessages([]), []);

  // ── Cleanup on unmount ────────────────────────────────────────────────
  useEffect(() => {
    return () => { wsRef.current?.close(1001, 'Component unmounted'); };
  }, []);

  return {
    status,
    username,
    onlineUsers,
    joinedRooms,
    messages,
    historyEntries,
    connect,
    disconnect,
    sendPublic,
    sendPrivate,
    createRoom,
    joinRoom,
    leaveRoom,
    sendRoomMessage,
    requestUserList,
    requestRoomList,
    requestHistory,
    clearMessages,
  };
}

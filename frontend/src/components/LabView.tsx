/**
 * LabView.tsx — System Lab: live stats, protocol inspector, raw frame sender.
 */
import { useState, useEffect, useRef } from 'react';
import type { ConnectionStatus, ChatMessage } from '../types';

interface RawLogEntry {
  ts: string;
  dir: 'in' | 'out' | 'sys' | 'err';
  text: string;
}

interface LabViewProps {
  status: ConnectionStatus;
  username: string | null;
  onlineUsers: string[];
  joinedRooms: string[];
  messages: ChatMessage[];
  onRequestUsers: () => void;
  onRequestRooms: () => void;
  onRequestHistory: () => void;
  onClearMessages: () => void;
  onSendRaw: (json: string) => void;
}

const nowStr = () => new Date().toLocaleTimeString([], { hour12: false });

const STATUS_LABELS: Record<ConnectionStatus, string> = {
  disconnected: 'Disconnected',
  connecting:   'Connecting…',
  connected:    'Connected',
  error:        'Error',
};

export default function LabView({
  status,
  username,
  onlineUsers,
  joinedRooms,
  messages,
  onRequestUsers,
  onRequestRooms,
  onRequestHistory,
  onClearMessages,
  onSendRaw,
}: LabViewProps) {
  const [rawInput, setRawInput] = useState('');
  const [rawErr, setRawErr] = useState('');
  const [log, setLog] = useState<RawLogEntry[]>([
    { ts: nowStr(), dir: 'sys', text: 'System Lab ready. Protocol: v1 JSON over WebSocket via bridge.' },
  ]);
  const logRef = useRef<HTMLDivElement>(null);

  const appendLog = (dir: RawLogEntry['dir'], text: string) =>
    setLog(prev => [...prev.slice(-300), { ts: nowStr(), dir, text }]);

  useEffect(() => {
    if (logRef.current) logRef.current.scrollTop = logRef.current.scrollHeight;
  }, [log]);

  const prevLen = useRef(0);
  useEffect(() => {
    const newMsgs = messages.slice(prevLen.current);
    prevLen.current = messages.length;
    newMsgs.forEach(m => {
      if (m.kind === 'error') appendLog('err', `[ERR] ${m.body}`);
      else if (m.kind === 'system') appendLog('sys', m.body);
      else if (m.own) appendLog('out', `→ ${m.kind}${m.room ? ' #'+m.room : m.target ? ' →'+m.target : ''}: ${m.body}`);
      else appendLog('in', `← ${m.kind} [${m.sender}]: ${m.body}`);
    });
  }, [messages]);

  const handleSendRaw = () => {
    const raw = rawInput.trim();
    if (!raw) return;
    try {
      JSON.parse(raw);
    } catch {
      setRawErr('Invalid JSON — parse error');
      return;
    }
    setRawErr('');
    onSendRaw(raw);
    appendLog('out', `RAW → ${raw}`);
    setRawInput('');
  };

  return (
    <div className="workspace">
      <div className="workspace__header">
        <div className="workspace__header-left">
          <h2>System Lab</h2>
          <span className={`conn-badge ${status}`}>
            <span className="conn-badge__dot" />
            {STATUS_LABELS[status]}
          </span>
        </div>
      </div>

      <div className="workspace__body" style={{ overflowY: 'auto', padding: '20px', display: 'flex', flexDirection: 'column', gap: 18 }}>
        {/* Stats */}
        <div className="grid-2">
          <div className="stat-card">
            <div className="stat-card__value">{onlineUsers.length}</div>
            <div className="stat-card__label">Users Online</div>
          </div>
          <div className="stat-card">
            <div className="stat-card__value">{joinedRooms.length}</div>
            <div className="stat-card__label">Rooms Joined</div>
          </div>
          <div className="stat-card">
            <div className="stat-card__value">{messages.length}</div>
            <div className="stat-card__label">Messages (session)</div>
          </div>
          <div className="stat-card">
            <div className="stat-card__value" style={{ fontSize: 15, fontWeight: 600 }}>
              {username ?? '—'}
            </div>
            <div className="stat-card__label">Registered as</div>
          </div>
        </div>

        {/* Quick commands */}
        <div className="lab-section">
          <div className="lab-section__header"><h3>Quick Commands</h3></div>
          <div className="lab-section__body" style={{ display: 'flex', flexWrap: 'wrap', gap: 8 }}>
            <button id="lab-user-list" className="btn btn-secondary" style={{ fontSize: 12 }} onClick={onRequestUsers}>
              user_list_req
            </button>
            <button id="lab-room-list" className="btn btn-secondary" style={{ fontSize: 12 }} onClick={onRequestRooms}>
              room_list_req
            </button>
            <button id="lab-history" className="btn btn-secondary" style={{ fontSize: 12 }} onClick={onRequestHistory}>
              history_req (public)
            </button>
            <button
              id="lab-clear"
              className="btn btn-secondary"
              style={{ fontSize: 12, color: 'var(--error)' }}
              onClick={() => { onClearMessages(); appendLog('sys', 'Message buffer cleared'); }}
            >
              Clear Buffer
            </button>
          </div>
        </div>

        {/* Raw frame sender */}
        <div className="lab-section">
          <div className="lab-section__header"><h3>Send Raw Frame (Ctrl+Enter)</h3></div>
          <div className="lab-section__body" style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
            <p className="text-xs text-secondary">
              Sends a raw JSON v1 frame via the bridge to the C++ server. Must be valid JSON.
            </p>
            <div style={{ display: 'flex', gap: 8 }}>
              <textarea
                id="raw-frame-input"
                className="input-field"
                style={{ flex: 1, height: 68, resize: 'vertical', fontFamily: 'monospace', fontSize: 12 }}
                placeholder='{"version":1,"type":"user_list_req","id":"lab-1","page":1,"limit":50}'
                value={rawInput}
                onChange={e => { setRawInput(e.target.value); setRawErr(''); }}
                onKeyDown={e => { if (e.key === 'Enter' && e.ctrlKey) handleSendRaw(); }}
              />
              <button
                id="send-raw-btn"
                className="btn btn-primary"
                style={{ height: 68, padding: '0 14px', flexShrink: 0 }}
                onClick={handleSendRaw}
                disabled={!rawInput.trim() || status !== 'connected'}
              >
                Send
              </button>
            </div>
            {rawErr && <p className="form-error">{rawErr}</p>}
          </div>
        </div>

        {/* Event log */}
        <div className="lab-section">
          <div className="lab-section__header"><h3>Event Log</h3></div>
          <div
            ref={logRef}
            className="raw-log"
            style={{ padding: '10px 14px', maxHeight: 240 }}
            aria-label="Event log"
          >
            {log.map((e, i) => (
              <div key={i} className="raw-log__line">
                <span className="raw-log__ts">{e.ts}</span>
                <span className={`raw-log__${e.dir}`}>{e.text}</span>
              </div>
            ))}
          </div>
        </div>

        {/* Protocol reference */}
        <div className="lab-section">
          <div className="lab-section__header"><h3>Protocol Quick Reference (v1)</h3></div>
          <div className="lab-section__body">
            <div className="code-block">{`// Register (first message — locks connection to v1)
{"version":1,"type":"register_req","id":"r1","username":"alice"}
// → register_ack{status:"ok",username:"alice"}

// Public broadcast (sender gets msg_ack; others get msg_broadcast)
{"version":1,"type":"msg_send","id":"r2","content":"Hello!","to":"","room":""}
// → msg_ack{id:"r2",status:"ok",msg_id:"msg-1"}   (server stored it)

// Private message (sender gets msg_direct_ack; recipient gets msg_direct)
{"version":1,"type":"msg_direct_req","id":"r3","to":"bob","content":"Hey"}
// → msg_direct_ack{id:"r3",status:"ok",msg_id:"msg-2","to":"bob"}

// Create room (auto-joins)
{"version":1,"type":"room_create_req","id":"r4","room":"general"}

// Join room
{"version":1,"type":"room_join_req","id":"r5","room":"general"}

// Room message (sender gets room_msg_ack; members get room_msg)
{"version":1,"type":"room_msg_req","id":"r6","room":"general","content":"Hi!"}

// User list
{"version":1,"type":"user_list_req","id":"r7","page":1,"limit":50}

// History (public)
{"version":1,"type":"history_req","id":"r8","conversation_type":"public","limit":20}

// History (direct)
{"version":1,"type":"history_req","id":"r9","conversation_type":"direct","with":"bob","limit":20}

// Graceful disconnect
{"version":1,"type":"disconnect_req","id":"r10"}`}</div>
          </div>
        </div>
      </div>
    </div>
  );
}

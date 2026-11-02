/**
 * LabView.tsx — System Lab: real connection metrics, protocol workbench,
 * and embedded Protocol Lens with privacy redaction.
 */

import { useState, useEffect } from 'react';
import type { ConnectionStatus, ChatMessage, ProtocolFrame } from '../types';
import ProtocolLens from './ProtocolLens';
import { IcLab, IcSend } from './icons';

interface LabViewProps {
  status: ConnectionStatus;
  username: string | null;
  onlineUsers: string[];
  joinedRooms: string[];
  messages: ChatMessage[];
  protocolFrames: ProtocolFrame[];
  connectedAt: Date | null;
  inboundCount: number;
  outboundCount: number;
  inboundBytes: number;
  outboundBytes: number;
  onRequestUsers: () => void;
  onRequestRooms: () => void;
  onRequestHistory: () => void;
  onClearMessages: () => void;
  onClearFrames: () => void;
  onSendRaw: (json: string) => boolean;
}

const STATUS_LABELS: Record<ConnectionStatus, string> = {
  disconnected: 'Disconnected',
  connecting:   'Connecting…',
  connected:    'Connected (TCP/WS Active)',
  error:        'Connection Error',
};

export default function LabView({
  status,
  username,
  onlineUsers,
  joinedRooms,
  messages,
  protocolFrames,
  connectedAt,
  inboundCount,
  outboundCount,
  inboundBytes,
  outboundBytes,
  onRequestUsers,
  onRequestRooms,
  onRequestHistory,
  onClearMessages,
  onClearFrames,
  onSendRaw,
}: LabViewProps) {
  const [activeTab, setActiveTab] = useState<'metrics' | 'lens' | 'workbench'>('metrics');
  const [rawInput, setRawInput] = useState('');
  const [rawErr, setRawErr] = useState('');
  const [lastSentStatus, setLastSentStatus] = useState<string | null>(null);

  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    if (!connectedAt) return;
    const interval = setInterval(() => setNow(Date.now()), 1000);
    return () => clearInterval(interval);
  }, [connectedAt]);

  const uptimeStr = (!connectedAt) ? '0s (Not connected)' : (() => {
    const secs = Math.max(0, Math.floor((now - connectedAt.getTime()) / 1000));
    const mins = Math.floor(secs / 60);
    const hrs = Math.floor(mins / 60);
    if (hrs > 0) return `${hrs}h ${mins % 60}m ${secs % 60}s`;
    if (mins > 0) return `${mins}m ${secs % 60}s`;
    return `${secs}s`;
  })();

  const handleSendRaw = () => {
    const raw = rawInput.trim();
    if (!raw) return;
    try {
      JSON.parse(raw);
    } catch {
      setRawErr('Invalid JSON syntax — check quotes, commas, and braces.');
      return;
    }
    setRawErr('');
    const ok = onSendRaw(raw);
    if (ok) {
      setLastSentStatus(`Sent at ${new Date().toLocaleTimeString()} (${new TextEncoder().encode(raw).length + 1} wire bytes)`);
      setRawInput('');
    } else {
      setRawErr('Failed to dispatch — socket closed or message oversized (>2048 bytes).');
    }
  };

  const loadPreset = (preset: Record<string, unknown>) => {
    setRawInput(JSON.stringify(preset, null, 2));
    setRawErr('');
  };

  return (
    <div className="workspace">
      <div className="workspace__header">
        <div className="workspace__header-left">
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <IcLab size={18} />
            <h2>System Lab & Diagnostics</h2>
          </div>
          <span className={`conn-badge ${status}`}>
            <span className="conn-badge__dot" />
            {STATUS_LABELS[status]}
          </span>
        </div>
      </div>

      <div className="workspace__body" style={{ overflowY: 'auto', padding: '18px 24px', display: 'flex', flexDirection: 'column', gap: 16 }}>
        {/* Navigation Tabs */}
        <div className="tab-group" style={{ alignSelf: 'flex-start' }}>
          <button
            className={`tab-btn ${activeTab === 'metrics' ? 'active' : ''}`}
            onClick={() => setActiveTab('metrics')}
          >
            Connection Metrics
          </button>
          <button
            className={`tab-btn ${activeTab === 'lens' ? 'active' : ''}`}
            onClick={() => setActiveTab('lens')}
          >
            Protocol Lens ({protocolFrames.length} frames)
          </button>
          <button
            className={`tab-btn ${activeTab === 'workbench' ? 'active' : ''}`}
            onClick={() => setActiveTab('workbench')}
          >
            Interactive Workbench
          </button>
        </div>

        {/* METRICS TAB */}
        {activeTab === 'metrics' && (
          <div style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
            {/* Real Stats Grid */}
            <div className="grid-2">
              <div className="stat-card">
                <div className="stat-card__value mono">{onlineUsers.length}</div>
                <div className="stat-card__label">Active Registered Users</div>
              </div>
              <div className="stat-card">
                <div className="stat-card__value mono">{joinedRooms.length}</div>
                <div className="stat-card__label">Rooms Currently Joined</div>
              </div>
              <div className="stat-card">
                <div className="stat-card__value mono">{inboundCount} / {outboundCount}</div>
                <div className="stat-card__label">Frames (Inbound / Outbound)</div>
              </div>
              <div className="stat-card">
                <div className="stat-card__value mono">
                  {(inboundBytes / 1024).toFixed(1)} KB / {(outboundBytes / 1024).toFixed(1)} KB
                </div>
                <div className="stat-card__label">Wire Volume (In / Out)</div>
              </div>
            </div>

            {/* Session Info */}
            <div className="lab-section">
              <div className="lab-section__header">
                <h3 style={{ margin: 0, fontSize: 13 }}>Verified Connection Invariants</h3>
              </div>
              <div className="lab-section__body">
                <div className="lens-attributes-table">
                  <div className="attr-row">
                    <span className="attr-key">Session Username</span>
                    <span className="attr-val mono">{username ?? 'Not registered'}</span>
                  </div>
                  <div className="attr-row">
                    <span className="attr-key">Session Duration</span>
                    <span className="attr-val">{uptimeStr}</span>
                  </div>
                  <div className="attr-row">
                    <span className="attr-key">Protocol Standard</span>
                    <span className="attr-val">C++17 TCP Chat v1 JSON (strict \n framing, 2048 B limit)</span>
                  </div>
                  <div className="attr-row">
                    <span className="attr-key">Bridge Session Isolation</span>
                    <span className="attr-val" style={{ color: 'var(--accent)' }}>
                      1:1 Dedicated TCP Socket per Browser Session
                    </span>
                  </div>
                  <div className="attr-row">
                    <span className="attr-key">Persistence Guarantee</span>
                    <span className="attr-val" style={{ color: 'var(--accent)' }}>
                      SQLite commit-before-ACK (Zero false acknowledgements)
                    </span>
                  </div>
                </div>
              </div>
            </div>

            {/* Quick Actions */}
            <div className="lab-section">
              <div className="lab-section__header">
                <h3 style={{ margin: 0, fontSize: 13 }}>Standard Probes</h3>
              </div>
              <div className="lab-section__body" style={{ display: 'flex', flexWrap: 'wrap', gap: 8 }}>
                <button id="lab-user-list" className="btn btn-secondary" style={{ fontSize: 12 }} onClick={onRequestUsers}>
                  Query user_list_req
                </button>
                <button id="lab-room-list" className="btn btn-secondary" style={{ fontSize: 12 }} onClick={onRequestRooms}>
                  Query room_list_req
                </button>
                <button id="lab-history" className="btn btn-secondary" style={{ fontSize: 12 }} onClick={onRequestHistory}>
                  Query history_req (public)
                </button>
                <button
                  id="lab-clear"
                  className="btn btn-secondary"
                  style={{ fontSize: 12, color: 'var(--error)' }}
                  onClick={onClearMessages}
                >
                  Clear Chat Buffer ({messages.length})
                </button>
              </div>
            </div>
          </div>
        )}

        {/* PROTOCOL LENS TAB */}
        {activeTab === 'lens' && (
          <ProtocolLens
            frames={protocolFrames}
            onClear={onClearFrames}
          />
        )}

        {/* WORKBENCH TAB */}
        {activeTab === 'workbench' && (
          <div style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
            {/* Presets */}
            <div className="lab-section">
              <div className="lab-section__header">
                <h3 style={{ margin: 0, fontSize: 13 }}>Quick Protocol Frame Presets</h3>
              </div>
              <div className="lab-section__body" style={{ display: 'flex', flexWrap: 'wrap', gap: 6 }}>
                <button
                  className="btn btn-ghost"
                  style={{ fontSize: 11, border: '1px solid var(--border)' }}
                  onClick={() => loadPreset({ version: 1, type: 'user_list_req', id: `probe-${Date.now()}`, page: 1, limit: 50 })}
                >
                  user_list_req
                </button>
                <button
                  className="btn btn-ghost"
                  style={{ fontSize: 11, border: '1px solid var(--border)' }}
                  onClick={() => loadPreset({ version: 1, type: 'room_list_req', id: `probe-${Date.now()}`, page: 1, limit: 50 })}
                >
                  room_list_req
                </button>
                <button
                  className="btn btn-ghost"
                  style={{ fontSize: 11, border: '1px solid var(--border)' }}
                  onClick={() => loadPreset({ version: 1, type: 'history_req', id: `probe-${Date.now()}`, conversation_type: 'public', limit: 10 })}
                >
                  history_req (public)
                </button>
                <button
                  className="btn btn-ghost"
                  style={{ fontSize: 11, border: '1px solid var(--border)' }}
                  onClick={() => loadPreset({ version: 1, type: 'msg_send', id: `probe-${Date.now()}`, content: 'Ping from System Lab workbench', to: '', room: '' })}
                >
                  msg_send (ping)
                </button>
              </div>
            </div>

            {/* Raw JSON Frame Sender */}
            <div className="lab-section">
              <div className="lab-section__header">
                <h3 style={{ margin: 0, fontSize: 13 }}>Raw JSON Wire Frame Dispatcher</h3>
                <span style={{ fontSize: 11, color: 'var(--text-secondary)' }}>
                  Sends exact newline-delimited JSON frame to C++ TCP server via bridge
                </span>
              </div>
              <div className="lab-section__body" style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
                <textarea
                  id="raw-frame-input"
                  className="input-field"
                  style={{ height: 110, fontFamily: 'monospace', fontSize: 12, lineHeight: 1.4 }}
                  placeholder='{"version": 1, "type": "user_list_req", "id": "test-1", "page": 1, "limit": 50}'
                  value={rawInput}
                  onChange={e => { setRawInput(e.target.value); setRawErr(''); }}
                />

                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                  <span style={{ fontSize: 11, color: rawErr ? 'var(--error)' : 'var(--text-secondary)' }}>
                    {rawErr || lastSentStatus || 'Frames must satisfy the 2048-byte limit and strict JSON specification.'}
                  </span>

                  <button
                    id="send-raw-btn"
                    className="btn btn-primary"
                    style={{ height: 34, padding: '0 18px', gap: 6 }}
                    onClick={handleSendRaw}
                    disabled={!rawInput.trim() || status !== 'connected'}
                  >
                    <IcSend size={13} /> Dispatch Frame
                  </button>
                </div>
              </div>
            </div>

            {/* Protocol Spec Reference */}
            <div className="lab-section">
              <div className="lab-section__header">
                <h3 style={{ margin: 0, fontSize: 13 }}>Protocol Contract Rules (v1)</h3>
              </div>
              <div className="lab-section__body" style={{ fontSize: 12, lineHeight: 1.6, color: 'var(--text-primary)' }}>
                <ul style={{ margin: 0, paddingLeft: 18 }}>
                  <li><strong>First Frame Isolation:</strong> First received frame identifies protocol mode (v1 structured vs v0 legacy). Once set, it cannot switch.</li>
                  <li><strong>Commit Before ACK:</strong> The server commits message content to SQLite before dispatching <code>msg_ack</code>. Storage failures immediately abort broadcasts.</li>
                  <li><strong>Sender Exclusion:</strong> Senders receive correlated acknowledgements (`msg_ack`, `room_msg_ack`, `msg_direct_ack`) and are excluded from peer broadcasts.</li>
                  <li><strong>Privacy Preservation:</strong> Private messages and direct history queries are restricted strictly to participants. Third parties receive <code>ERR_UNAUTHORIZED</code>.</li>
                  <li><strong>Room Lifecycles:</strong> Chat rooms are ephemeral in-memory entities. When the last member leaves, the room is deleted.</li>
                </ul>
              </div>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}

/**
 * HistoryView.tsx — SQLite-backed message history with cursor-based pagination.
 * Supports public, direct (with a peer), and room history, adhering strictly
 * to server authorization rules and wire limit bounds.
 */

import { useState } from 'react';
import type { HistoryEntry, ChatMessage } from '../types';
import { IcHistory, IcRefresh, IcInfo, IcFlightRecorder, IcClose } from './icons';
import FlightRecorder from './FlightRecorder';

interface HistoryViewProps {
  username?: string;
  joinedRooms: string[];
  onlineUsers: string[];
  historyEntries: HistoryEntry[];
  historyHasMore: boolean;
  historyCount: number;
  historyError?: string | null;
  onClearHistoryError?: () => void;
  onRequestHistory: (
    conversationType: 'public' | 'direct' | 'room',
    opts?: {
      with?: string;
      room?: string;
      limit?: number;
      before_id?: string;
      after_id?: string;
    }
  ) => void;
}

const formatTs = (ts: number) => {
  try {
    return new Date(ts * 1000).toLocaleString([], {
      month: 'short',
      day: 'numeric',
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
    });
  } catch {
    return String(ts);
  }
};

export default function HistoryView({
  username = '',
  joinedRooms,
  onlineUsers,
  historyEntries,
  historyHasMore,
  historyCount,
  historyError,
  onClearHistoryError,
  onRequestHistory,
}: HistoryViewProps) {
  const [convType, setConvType] = useState<'public' | 'direct' | 'room'>('public');
  const [withUser, setWithUser] = useState('');
  const [forRoom, setForRoom] = useState('');
  const [limit, setLimit] = useState(20);
  const [activeCursor, setActiveCursor] = useState<{ mode: 'latest' | 'before' | 'after'; id?: string }>({ mode: 'latest' });
  const [validationErr, setValidationErr] = useState('');
  const [inspectedMessage, setInspectedMessage] = useState<ChatMessage | null>(null);

  const oldestMsgId = historyEntries.length > 0 ? historyEntries[0]?.msg_id : undefined;
  const newestMsgId = historyEntries.length > 0 ? historyEntries[historyEntries.length - 1]?.msg_id : undefined;

  const validate = (): boolean => {
    if (convType === 'direct' && !withUser.trim()) {
      setValidationErr('Participant username ("with") is required for direct message history.');
      return false;
    }
    if (convType === 'room' && !forRoom.trim()) {
      setValidationErr('Room name ("room") is required for room message history.');
      return false;
    }
    setValidationErr('');
    return true;
  };

  const handleFetchLatest = () => {
    if (!validate()) return;
    setActiveCursor({ mode: 'latest' });
    onRequestHistory(convType, {
      with: convType === 'direct' ? withUser.trim() : undefined,
      room: convType === 'room' ? forRoom.trim() : undefined,
      limit,
    });
  };

  const handleFetchOlder = () => {
    if (!validate()) return;
    if (!oldestMsgId) return;
    setActiveCursor({ mode: 'before', id: oldestMsgId });
    onRequestHistory(convType, {
      with: convType === 'direct' ? withUser.trim() : undefined,
      room: convType === 'room' ? forRoom.trim() : undefined,
      limit,
      before_id: oldestMsgId,
    });
  };

  const handleFetchNewer = () => {
    if (!validate()) return;
    if (!newestMsgId) return;
    setActiveCursor({ mode: 'after', id: newestMsgId });
    onRequestHistory(convType, {
      with: convType === 'direct' ? withUser.trim() : undefined,
      room: convType === 'room' ? forRoom.trim() : undefined,
      limit,
      after_id: newestMsgId,
    });
  };

  return (
    <div className="workspace">
      <div className="workspace__header">
        <div className="workspace__header-left">
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <IcHistory size={18} />
            <h2>Message History</h2>
          </div>
          {historyEntries.length > 0 && (
            <span className="chip accent">
              {historyCount || historyEntries.length} messages {historyHasMore ? '(has more)' : ''}
            </span>
          )}
        </div>
        <div className="workspace__header-right">
          <button
            id="fetch-history-btn"
            className="btn btn-primary"
            style={{ fontSize: 12, height: 32 }}
            onClick={handleFetchLatest}
          >
            <IcRefresh size={13} /> Refresh Latest
          </button>
        </div>
      </div>

      <div className="workspace__body" style={{ display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
        {/* Filter controls */}
        <div style={{ padding: '14px 24px', borderBottom: '1px solid var(--border)', background: 'var(--panel)', flexShrink: 0 }}>
          <div style={{ display: 'flex', gap: 12, flexWrap: 'wrap', alignItems: 'flex-end' }}>
            <div className="form-group" style={{ minWidth: 140 }}>
              <label className="form-label">Conversation</label>
              <select
                id="history-type-select"
                className="input-field"
                value={convType}
                onChange={e => {
                  setConvType(e.target.value as 'public' | 'direct' | 'room');
                  setValidationErr('');
                }}
              >
                <option value="public">Public Broadcasts</option>
                <option value="direct">Direct Message (DM)</option>
                <option value="room">Chat Room</option>
              </select>
            </div>

            {convType === 'direct' && (
              <div className="form-group" style={{ minWidth: 160 }}>
                <label className="form-label">With user (Required)</label>
                <input
                  id="history-with-input"
                  className="input-field"
                  placeholder="e.g. Alice"
                  value={withUser}
                  onChange={e => { setWithUser(e.target.value); setValidationErr(''); }}
                  list="user-datalist"
                />
                <datalist id="user-datalist">
                  {onlineUsers.map(u => <option key={u} value={u} />)}
                </datalist>
              </div>
            )}

            {convType === 'room' && (
              <div className="form-group" style={{ minWidth: 160 }}>
                <label className="form-label">Room name (Required)</label>
                <input
                  id="history-room-input"
                  className="input-field"
                  placeholder="e.g. general"
                  value={forRoom}
                  onChange={e => {
                    setForRoom(e.target.value);
                    setValidationErr('');
                    onClearHistoryError?.();
                  }}
                  list="history-rooms-datalist"
                />
                <datalist id="history-rooms-datalist">
                  {joinedRooms.map(r => <option key={r} value={r}>#{r} (Joined)</option>)}
                </datalist>
              </div>
            )}

            <div className="form-group" style={{ minWidth: 80 }}>
              <label className="form-label">Page limit</label>
              <select
                id="history-limit-select"
                className="input-field"
                value={limit}
                onChange={e => setLimit(Number(e.target.value))}
              >
                {[10, 20, 50].map(n => <option key={n} value={n}>{n} msgs</option>)}
              </select>
            </div>

            <button
              className="btn btn-primary"
              style={{ height: 36, padding: '0 16px' }}
              onClick={handleFetchLatest}
            >
              Fetch Latest
            </button>
          </div>

          {validationErr && (
            <div style={{ color: 'var(--error, #B54747)', fontSize: 12, marginTop: 8 }}>
              {validationErr}
            </div>
          )}

          {historyError && (
            <div className="alert-box error" style={{ marginTop: 8, display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                <span style={{ fontWeight: 600 }}>Server Authorization / Query Rejection:</span>
                <span className="mono">{historyError}</span>
              </div>
              {onClearHistoryError && (
                <button className="btn-icon" onClick={onClearHistoryError} title="Dismiss">
                  <IcClose size={12} />
                </button>
              )}
            </div>
          )}

          {/* Authorization notice */}
          <div style={{ marginTop: 10, fontSize: 11, color: 'var(--text-secondary)', display: 'flex', alignItems: 'center', gap: 6 }}>
            <IcInfo size={13} />
            <span>
              {convType === 'public' && 'Public history is accessible to any registered client.'}
              {convType === 'direct' && 'Direct history is private and accessible only to the 2 participants (server rejects third parties).'}
              {convType === 'room' && 'Room history is restricted to active members of the room (non-members receive ERR_NOT_IN_ROOM).'}
            </span>
          </div>
        </div>

        {/* Cursor Pagination Bar */}
        {historyEntries.length > 0 && (
          <div className="cursor-bar">
            <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
              <span style={{ fontSize: 11, color: 'var(--text-secondary)' }}>
                Cursor mode: <strong>{activeCursor.mode}</strong>
                {activeCursor.id && <code style={{ marginLeft: 4 }}>({activeCursor.id})</code>}
              </span>
              {historyHasMore && (
                <span className="chip" style={{ fontSize: 10, background: 'var(--accent-tint)' }}>
                  Wire Bounded: has_more=true
                </span>
              )}
            </div>

            <div style={{ display: 'flex', gap: 8 }}>
              <button
                className="btn btn-secondary"
                onClick={handleFetchOlder}
                title="Fetch older messages strictly before oldest loaded message"
                style={{ height: 28, fontSize: 11 }}
              >
                ← Older (before_id)
              </button>
              <button
                className="btn btn-secondary"
                onClick={handleFetchNewer}
                title="Fetch newer messages strictly after newest loaded message"
                style={{ height: 28, fontSize: 11 }}
              >
                Newer (after_id) →
              </button>
              <button
                className="btn btn-ghost"
                onClick={handleFetchLatest}
                style={{ height: 28, fontSize: 11 }}
              >
                Reset
              </button>
            </div>
          </div>
        )}

        {/* Results */}
        <div style={{ display: 'flex', flex: 1, minHeight: 0, position: 'relative' }}>
          <div style={{ flex: 1, overflowY: 'auto', padding: '16px 24px' }}>
            {historyEntries.length === 0 ? (
              <div className="empty-state" style={{ marginTop: 40 }}>
                <div className="empty-state__icon"><IcHistory size={24} /></div>
                <div className="empty-state__title">No history records loaded</div>
                <div className="empty-state__desc">
                  Select conversation parameters above and click Fetch to retrieve durable SQLite messages.
                </div>
              </div>
            ) : (
              <div className="history-list">
                {historyEntries.map(entry => (
                  <div key={entry.msg_id} className="history-row" id={`history-${entry.msg_id}`}>
                    <div className="history-row__meta">
                      <span className="mono history-msg-id" style={{ color: 'var(--accent)', fontWeight: 600 }}>
                        {entry.msg_id}
                      </span>
                      <span className="history-row__time">{formatTs(entry.timestamp)}</span>
                    </div>

                    <div className="history-row__body">
                      <div className="history-row__sender" style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
                        <div>
                          <strong>{entry.from}</strong>
                          {entry.type === 'direct' && (
                            <span className="chip" style={{ marginLeft: 6, fontSize: 10 }}>→ {entry.to}</span>
                          )}
                          {entry.type === 'room' && entry.room && (
                            <span className="chip accent" style={{ marginLeft: 6, fontSize: 10 }}>#{entry.room}</span>
                          )}
                          <span className="chip" style={{ marginLeft: 6, fontSize: 9 }}>SQLite Verified</span>
                        </div>
                        <button
                          className="btn-msg-inspect"
                          title="Inspect in Message Flight Recorder"
                          onClick={() => {
                            const chatMsg: ChatMessage = {
                              id: `hist-${entry.msg_id}`,
                              msgId: entry.msg_id,
                              protocolVersion: 1,
                              rawType: entry.type === 'direct' ? 'msg_direct' : entry.type === 'room' ? 'room_msg' : 'msg_broadcast',
                              kind: entry.type === 'direct' ? 'private' : entry.type === 'room' ? 'room' : 'public',
                              sender: entry.from,
                              target: entry.to,
                              room: entry.room,
                              body: entry.content,
                              timestamp: new Date(entry.timestamp * 1000),
                              own: entry.from === username,
                              status: 'accepted',
                              persistence: 'confirmed',
                              rawPayload: JSON.stringify({
                                version: 1,
                                type: entry.type === 'direct' ? 'msg_direct' : entry.type === 'room' ? 'room_msg' : 'msg_broadcast',
                                msg_id: entry.msg_id,
                                from: entry.from,
                                ...(entry.to ? { to: entry.to } : {}),
                                ...(entry.room ? { room: entry.room } : {}),
                                content: entry.content,
                                timestamp: entry.timestamp,
                              }, null, 2),
                            };
                            setInspectedMessage(chatMsg);
                          }}
                        >
                          <IcFlightRecorder size={12} />
                        </button>
                      </div>
                      <div className="history-row__text">{entry.content}</div>
                    </div>
                  </div>
                ))}
              </div>
            )}
          </div>

          {inspectedMessage && (
            <FlightRecorder
              message={inspectedMessage}
              onClose={() => setInspectedMessage(null)}
            />
          )}
        </div>
      </div>
    </div>
  );
}

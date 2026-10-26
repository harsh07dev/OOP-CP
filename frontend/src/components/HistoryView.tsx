/**
 * HistoryView.tsx — Message history using history_req protocol.
 * Supports public, direct (with a peer), and room history.
 */
import { useState } from 'react';
import type { HistoryEntry } from '../types';
import { IcHistory, IcRefresh } from './icons';

interface HistoryViewProps {
  joinedRooms: string[];
  onlineUsers: string[];
  historyEntries: HistoryEntry[];
  onRequestHistory: (
    conversationType: 'public' | 'direct' | 'room',
    opts?: { with?: string; room?: string; limit?: number }
  ) => void;
}

const formatTs = (ts: number) => {
  try {
    return new Date(ts * 1000).toLocaleString([], {
      month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit',
    });
  } catch { return String(ts); }
};

export default function HistoryView({
  joinedRooms,
  onlineUsers,
  historyEntries,
  onRequestHistory,
}: HistoryViewProps) {
  const [convType, setConvType] = useState<'public' | 'direct' | 'room'>('public');
  const [withUser, setWithUser] = useState('');
  const [forRoom, setForRoom] = useState('');
  const [limit, setLimit] = useState(20);

  const handleFetch = () => {
    const opts: { with?: string; room?: string; limit: number } = { limit };
    if (convType === 'direct' && withUser) opts.with = withUser;
    if (convType === 'room' && forRoom) opts.room = forRoom;
    onRequestHistory(convType, opts);
  };

  return (
    <div className="workspace">
      <div className="workspace__header">
        <div className="workspace__header-left">
          <h2>History</h2>
          {historyEntries.length > 0 && (
            <span className="chip">{historyEntries.length} messages</span>
          )}
        </div>
        <div className="workspace__header-right">
          <button
            id="fetch-history-btn"
            className="btn btn-primary"
            style={{ fontSize: 12, height: 32 }}
            onClick={handleFetch}
          >
            <IcRefresh size={13} /> Fetch
          </button>
        </div>
      </div>

      <div className="workspace__body" style={{ display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
        {/* Filter controls */}
        <div style={{ padding: '12px 20px', borderBottom: '1px solid var(--border)', background: 'var(--panel)', flexShrink: 0 }}>
          <div style={{ display: 'flex', gap: 10, flexWrap: 'wrap', alignItems: 'flex-end' }}>
            <div className="form-group" style={{ minWidth: 140 }}>
              <label className="form-label">Conversation type</label>
              <select
                id="history-type-select"
                className="input-field"
                value={convType}
                onChange={(e) => setConvType(e.target.value as 'public' | 'direct' | 'room')}
              >
                <option value="public">Public</option>
                <option value="direct">Direct (DM)</option>
                <option value="room">Room</option>
              </select>
            </div>

            {convType === 'direct' && (
              <div className="form-group" style={{ minWidth: 140 }}>
                <label className="form-label">With user</label>
                <input
                  id="history-with-input"
                  className="input-field"
                  placeholder="Username"
                  value={withUser}
                  onChange={(e) => setWithUser(e.target.value)}
                  list="user-datalist"
                />
                <datalist id="user-datalist">
                  {onlineUsers.map(u => <option key={u} value={u} />)}
                </datalist>
              </div>
            )}

            {convType === 'room' && (
              <div className="form-group" style={{ minWidth: 140 }}>
                <label className="form-label">Room</label>
                <select
                  id="history-room-select"
                  className="input-field"
                  value={forRoom}
                  onChange={(e) => setForRoom(e.target.value)}
                >
                  <option value="">Select a room</option>
                  {joinedRooms.map(r => <option key={r} value={r}>#{r}</option>)}
                </select>
              </div>
            )}

            <div className="form-group" style={{ minWidth: 80 }}>
              <label className="form-label">Limit</label>
              <select
                id="history-limit-select"
                className="input-field"
                value={limit}
                onChange={(e) => setLimit(Number(e.target.value))}
              >
                {[10, 20, 50].map(n => <option key={n} value={n}>{n}</option>)}
              </select>
            </div>

            <button
              className="btn btn-primary"
              style={{ height: 38, alignSelf: 'flex-end' }}
              onClick={handleFetch}
            >
              Load
            </button>
          </div>
        </div>

        {/* Results */}
        <div style={{ flex: 1, overflowY: 'auto', padding: '16px 24px' }}>
          {historyEntries.length === 0 ? (
            <div className="empty-state" style={{ marginTop: 40 }}>
              <div className="empty-state__icon"><IcHistory size={22} /></div>
              <div className="empty-state__title">No history loaded</div>
              <div className="empty-state__desc">
                Select a conversation type and click Fetch to retrieve stored messages.
                <br />
                <span style={{ fontSize: 11, marginTop: 8, display: 'block', color: 'var(--text-secondary)' }}>
                  Direct history requires you to be one of the two participants.
                  Room history requires active membership.
                </span>
              </div>
            </div>
          ) : (
            <div>
              {historyEntries.map((entry) => (
                <div key={entry.msg_id} className="history-row" id={`history-${entry.msg_id}`}>
                  <div className="history-row__time">{formatTs(entry.timestamp)}</div>
                  <div className="history-row__body">
                    <div className="history-row__sender">
                      {entry.from}
                      {entry.type === 'direct' && (
                        <span className="chip" style={{ marginLeft: 6, fontSize: 10 }}>→ {entry.to}</span>
                      )}
                      {entry.type === 'room' && entry.room && (
                        <span className="chip accent" style={{ marginLeft: 6, fontSize: 10 }}>#{entry.room}</span>
                      )}
                      <span className="text-xs text-secondary" style={{ marginLeft: 6, fontWeight: 400 }}>
                        {entry.msg_id}
                      </span>
                    </div>
                    <div className="history-row__text">{entry.content}</div>
                  </div>
                </div>
              ))}
            </div>
          )}
        </div>
      </div>
    </div>
  );
}

/**
 * RoomsView.tsx — Room discovery and management.
 * Rooms are created via room_create_req (auto-joins creator).
 * Joined via room_join_req. Server controls room state.
 */
import { useState } from 'react';
import type { ChatContext } from '../types';
import { IcHash, IcPlus, IcRefresh } from './icons';

interface RoomsViewProps {
  username: string;
  joinedRooms: string[];
  onJoinRoom: (room: string) => void;
  onCreateRoom: (room: string) => void;
  onLeaveRoom: (room: string) => void;
  onRequestRooms: () => void;
  onContextChange: (ctx: ChatContext) => void;
  onViewChange: (view: 'chats') => void;
}

const COLORS = [
  { bg: '#D9EEEA', color: '#247D73' },
  { bg: '#EEE9F5', color: '#6B4E9E' },
  { bg: '#FFF4E6', color: '#A86D22' },
  { bg: '#E6F0FF', color: '#2451A8' },
];
const roomColor = (name: string) => COLORS[name.charCodeAt(0) % COLORS.length];

// Room names: 1–32 chars, [a-zA-Z0-9_-]
const ROOM_PATTERN = /^[a-zA-Z0-9_-]{1,32}$/;

export default function RoomsView({
  joinedRooms,
  onJoinRoom,
  onCreateRoom,
  onLeaveRoom,
  onContextChange,
  onViewChange,
  onRequestRooms,
}: RoomsViewProps) {
  const [roomInput, setRoomInput] = useState('');
  const [roomErr, setRoomErr] = useState('');

  const validate = (r: string) => {
    if (!r) return 'Room name is required';
    if (!ROOM_PATTERN.test(r)) return 'Letters, digits, _ and - only (max 32)';
    return '';
  };

  const handleJoin = () => {
    const r = roomInput.trim();
    const err = validate(r);
    if (err) { setRoomErr(err); return; }
    setRoomErr('');
    if (joinedRooms.includes(r)) {
      setRoomErr('Already in this room');
      return;
    }
    // Try join; if not found, create
    onJoinRoom(r);
    setRoomInput('');
  };

  const handleCreate = () => {
    const r = roomInput.trim();
    const err = validate(r);
    if (err) { setRoomErr(err); return; }
    setRoomErr('');
    onCreateRoom(r);
    setRoomInput('');
  };

  return (
    <div className="workspace">
      <div className="workspace__header">
        <div className="workspace__header-left">
          <h2>Rooms</h2>
          <span className="chip accent">{joinedRooms.length} joined</span>
        </div>
        <div className="workspace__header-right">
          <button
            id="refresh-rooms-btn"
            className="btn btn-ghost btn-icon"
            title="Refresh rooms"
            onClick={onRequestRooms}
            aria-label="Refresh rooms"
          >
            <IcRefresh size={14} />
          </button>
        </div>
      </div>

      <div className="workspace__body" style={{ overflowY: 'auto', padding: '20px' }}>
        {/* Join / Create room */}
        <div className="lab-section" style={{ marginBottom: 24 }}>
          <div className="lab-section__header">
            <h3>Join or Create a Room</h3>
          </div>
          <div className="lab-section__body">
            <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap' }}>
              <input
                id="room-name-input"
                className="input-field"
                placeholder="Room name (letters, digits, _ -)"
                value={roomInput}
                onChange={(e) => { setRoomInput(e.target.value); setRoomErr(''); }}
                onKeyDown={(e) => e.key === 'Enter' && handleJoin()}
                style={{ flex: 1, minWidth: 160 }}
                maxLength={32}
              />
              <button
                id="join-room-btn"
                className="btn btn-secondary"
                onClick={handleJoin}
                disabled={!roomInput.trim()}
                title="Join an existing room"
              >
                Join
              </button>
              <button
                id="create-room-btn"
                className="btn btn-primary"
                onClick={handleCreate}
                disabled={!roomInput.trim()}
                title="Create a new room (auto-joins)"
              >
                <IcPlus size={14} /> Create
              </button>
            </div>
            {roomErr && <p className="form-error" style={{ marginTop: 6 }}>{roomErr}</p>}
            <p className="text-xs text-secondary" style={{ marginTop: 6 }}>
              Join connects to an existing room. Create makes a new one (you are auto-joined as first member).
            </p>
          </div>
        </div>

        <h4 style={{ marginBottom: 12 }}>Your Rooms</h4>

        {joinedRooms.length === 0 && (
          <div className="empty-state">
            <div className="empty-state__icon"><IcHash size={22} /></div>
            <div className="empty-state__title">No rooms joined</div>
            <div className="empty-state__desc">
              Type a room name above. Use "Join" for existing rooms, "Create" for new ones.
            </div>
          </div>
        )}

        <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
          {joinedRooms.map(room => {
            const col = roomColor(room);
            return (
              <div key={room} className="room-card" id={`room-card-${room}`}>
                <div className="room-card__icon" style={{ background: col.bg, color: col.color }}>
                  <IcHash size={16} />
                </div>
                <div className="room-card__info">
                  <div className="room-card__name">#{room}</div>
                  <div className="room-card__meta">Joined</div>
                </div>
                <div style={{ display: 'flex', gap: 6 }}>
                  <button
                    id={`open-room-${room}`}
                    className="btn btn-primary"
                    style={{ fontSize: 12, height: 30 }}
                    onClick={() => { onContextChange({ type: 'room', room }); onViewChange('chats'); }}
                  >
                    Open
                  </button>
                  <button
                    id={`leave-room-${room}`}
                    className="btn btn-secondary"
                    style={{ fontSize: 12, height: 30 }}
                    onClick={() => onLeaveRoom(room)}
                  >
                    Leave
                  </button>
                </div>
              </div>
            );
          })}
        </div>
      </div>
    </div>
  );
}

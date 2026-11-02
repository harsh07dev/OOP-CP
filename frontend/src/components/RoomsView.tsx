/**
 * RoomsView.tsx — Room discovery, membership, and lifecycle events.
 * Implements room listing, join, leave, member inspection, and real-time room events.
 */

import { useState } from 'react';
import type { ChatContext, RoomLifecycleEvent } from '../types';
import { IcHash, IcPlus, IcRefresh, IcClose, IcUser, IcInfo } from './icons';

interface RoomsViewProps {
  username: string;
  joinedRooms: string[];
  discoveredRooms: string[];
  roomMembers: Record<string, string[]>;
  roomEvents: RoomLifecycleEvent[];
  roomError?: string | null;
  onClearRoomError?: () => void;
  roomListPage?: number;
  roomListTotalPages?: number;
  totalRooms?: number;
  onJoinRoom: (room: string) => void;
  onCreateRoom: (room: string) => void;
  onLeaveRoom: (room: string) => void;
  onRequestRooms: (page?: number) => void;
  onRequestRoomMembers: (room: string) => void;
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

const ROOM_PATTERN = /^[a-zA-Z0-9_-]{1,32}$/;

export default function RoomsView({
  username: _username,
  joinedRooms,
  discoveredRooms,
  roomMembers,
  roomEvents,
  roomError,
  onClearRoomError,
  roomListPage = 1,
  roomListTotalPages = 1,
  totalRooms: _totalRooms,
  onJoinRoom,
  onCreateRoom,
  onLeaveRoom,
  onContextChange,
  onViewChange,
  onRequestRooms,
  onRequestRoomMembers,
}: RoomsViewProps) {
  const [roomInput, setRoomInput] = useState('');
  const [roomErr, setRoomErr] = useState('');
  const [selectedRoomForMembers, setSelectedRoomForMembers] = useState<string | null>(null);
  const [activeTab, setActiveTab] = useState<'rooms' | 'events'>('rooms');

  const validate = (r: string) => {
    if (!r) return 'Room name is required';
    if (!ROOM_PATTERN.test(r)) return 'Letters, digits, _ and - only (max 32)';
    return '';
  };

  const handleJoin = (target?: string) => {
    const r = (target ?? roomInput).trim().toLowerCase();
    const err = validate(r);
    if (err) { setRoomErr(err); return; }
    setRoomErr('');
    if (joinedRooms.includes(r)) {
      onContextChange({ type: 'room', room: r });
      onViewChange('chats');
      return;
    }
    onJoinRoom(r);
    if (!target) setRoomInput('');
  };

  const handleCreate = () => {
    const r = roomInput.trim().toLowerCase();
    const err = validate(r);
    if (err) { setRoomErr(err); return; }
    setRoomErr('');
    onCreateRoom(r);
    setRoomInput('');
  };

  const handleInspectMembers = (room: string) => {
    setSelectedRoomForMembers(room);
    onRequestRoomMembers(room);
  };

  // Combine discovered rooms and joined rooms into a unified unique set
  const allKnownRooms = Array.from(new Set([...discoveredRooms, ...joinedRooms])).sort();

  return (
    <div className="workspace">
      <div className="workspace__header">
        <div className="workspace__header-left">
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <IcHash size={18} />
            <h2>Rooms</h2>
          </div>
          <span className="chip accent">{joinedRooms.length} joined / {allKnownRooms.length} active</span>
        </div>
        <div className="workspace__header-right">
          <button
            id="refresh-rooms-btn"
            className="btn btn-ghost btn-icon"
            title="Refresh room list from server"
            onClick={() => onRequestRooms()}
            aria-label="Refresh rooms"
          >
            <IcRefresh size={14} />
          </button>
        </div>
      </div>

      <div className="workspace__body" style={{ overflowY: 'auto', padding: '20px 24px' }}>
        {/* Navigation Tabs */}
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 20 }}>
          <div className="tab-group">
            <button
              className={`tab-btn ${activeTab === 'rooms' ? 'active' : ''}`}
              onClick={() => setActiveTab('rooms')}
            >
              Room Directory ({allKnownRooms.length})
            </button>
            <button
              className={`tab-btn ${activeTab === 'events' ? 'active' : ''}`}
              onClick={() => setActiveTab('events')}
            >
              Room Lifecycle Events ({roomEvents.length})
            </button>
          </div>
        </div>

        {activeTab === 'rooms' && (
          <>
            {roomError && (
              <div className="alert-box error" style={{ marginBottom: 16, display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                  <span style={{ fontSize: 13, fontWeight: 600 }}>Server Rejection:</span>
                  <span className="mono" style={{ fontSize: 12 }}>{roomError}</span>
                </div>
                {onClearRoomError && (
                  <button className="btn-icon" onClick={onClearRoomError} title="Dismiss">
                    <IcClose size={12} />
                  </button>
                )}
              </div>
            )}

            {/* Create or Join Room bar */}
            <div className="lab-section" style={{ marginBottom: 24 }}>
              <div className="lab-section__header">
                <h3 style={{ margin: 0, fontSize: 13 }}>Create or Join a Room</h3>
                <span style={{ fontSize: 11, color: 'var(--text-secondary)' }}>
                  Alphanumeric, _ and - (case-insensitive, max 32 characters)
                </span>
              </div>
              <div className="lab-section__body">
                <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap' }}>
                  <div style={{ position: 'relative', flex: 1, minWidth: 200 }}>
                    <span style={{ position: 'absolute', left: 10, top: '50%', transform: 'translateY(-50%)', color: 'var(--text-secondary)' }}>#</span>
                    <input
                      id="room-name-input"
                      type="text"
                      className="input-field"
                      placeholder="room-name"
                      value={roomInput}
                      onChange={e => {
                        setRoomInput(e.target.value);
                        setRoomErr('');
                        onClearRoomError?.();
                      }}
                      onKeyDown={e => { if (e.key === 'Enter') handleJoin(); }}
                      style={{ paddingLeft: 26, height: 36 }}
                    />
                  </div>

                  <button
                    id="join-room-btn"
                    className="btn btn-primary"
                    onClick={() => handleJoin()}
                    style={{ height: 36, padding: '0 16px' }}
                  >
                    Join Room
                  </button>

                  <button
                    id="create-room-btn"
                    className="btn btn-secondary"
                    onClick={handleCreate}
                    style={{ height: 36, padding: '0 16px', gap: 6 }}
                  >
                    <IcPlus size={14} /> Create Room
                  </button>
                </div>

                {roomErr && (
                  <div style={{ color: 'var(--error, #B54747)', fontSize: 12, marginTop: 6 }}>
                    {roomErr}
                  </div>
                )}
              </div>
            </div>

            {/* Room List Grid */}
            <h4 style={{ marginBottom: 12, fontSize: 12, color: 'var(--text-secondary)', textTransform: 'uppercase', letterSpacing: '0.05em' }}>
              Active Chat Rooms ({allKnownRooms.length})
            </h4>

            {allKnownRooms.length === 0 ? (
              <div className="empty-state" style={{ marginTop: 20, padding: 32 }}>
                <div className="empty-state__icon">#</div>
                <div className="empty-state__title">No active rooms found</div>
                <div className="empty-state__desc">
                  Chat rooms are ephemeral in-memory channels. When created, they persist as long as at least one member is joined.
                </div>
              </div>
            ) : (
              <div className="room-card-grid">
                {allKnownRooms.map(room => {
                  const isJoined = joinedRooms.includes(room);
                  const col = roomColor(room);
                  const members = roomMembers[room];

                  return (
                    <div key={room} className={`room-card ${isJoined ? 'joined' : ''}`} id={`room-${room}`}>
                      <div className="room-card__header">
                        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                          <div className="room-avatar" style={{ background: col.bg, color: col.color }}>
                            #
                          </div>
                          <div>
                            <div className="room-card__title">#{room}</div>
                            <span className="room-card__meta">
                              {isJoined ? 'Joined Member' : 'Available on server'}
                            </span>
                          </div>
                        </div>

                        {isJoined && (
                          <span className="chip accent" style={{ fontSize: 10 }}>Member</span>
                        )}
                      </div>

                      <div className="room-card__actions">
                        {isJoined ? (
                          <>
                            <button
                              className="btn btn-primary"
                              style={{ flex: 1, height: 30, fontSize: 12 }}
                              onClick={() => {
                                onContextChange({ type: 'room', room });
                                onViewChange('chats');
                              }}
                            >
                              Open Chat
                            </button>
                            <button
                              className="btn btn-secondary"
                              style={{ height: 30, fontSize: 12, padding: '0 8px' }}
                              onClick={() => handleInspectMembers(room)}
                              title="Inspect members"
                            >
                              <IcUser size={13} /> {members ? members.length : 'Members'}
                            </button>
                            <button
                              className="btn btn-ghost"
                              style={{ height: 30, fontSize: 12, color: 'var(--error)' }}
                              onClick={() => onLeaveRoom(room)}
                              title="Leave room"
                            >
                              Leave
                            </button>
                          </>
                        ) : (
                          <>
                            <button
                              className="btn btn-secondary"
                              style={{ flex: 1, height: 30, fontSize: 12 }}
                              onClick={() => handleJoin(room)}
                            >
                              Join Room
                            </button>
                            <button
                              className="btn btn-ghost"
                              style={{ height: 30, fontSize: 11, color: 'var(--text-secondary)' }}
                              onClick={() => handleInspectMembers(room)}
                              title="Inspect member count"
                            >
                              Members?
                            </button>
                          </>
                        )}
                      </div>
                    </div>
                  );
                })}
              </div>
            )}

            {roomListTotalPages > 1 && (
              <div style={{ display: 'flex', justifyContent: 'center', alignItems: 'center', gap: 12, marginTop: 16 }}>
                <button
                  className="btn btn-secondary"
                  disabled={roomListPage <= 1}
                  onClick={() => onRequestRooms(roomListPage - 1)}
                  style={{ height: 28, fontSize: 11 }}
                >
                  ← Previous
                </button>
                <span style={{ fontSize: 12, color: 'var(--text-secondary)' }}>
                  Page {roomListPage} of {roomListTotalPages}
                </span>
                <button
                  className="btn btn-secondary"
                  disabled={roomListPage >= roomListTotalPages}
                  onClick={() => onRequestRooms(roomListPage + 1)}
                  style={{ height: 28, fontSize: 11 }}
                >
                  Next →
                </button>
              </div>
            )}
          </>
        )}

        {/* Room Lifecycle Events */}
        {activeTab === 'events' && (
          <div className="presence-log-panel">
            <div style={{ marginBottom: 12, fontSize: 12, color: 'var(--text-secondary)' }}>
              Real-time audit log of <code>room_user_joined</code>, <code>room_user_left</code>, and room creation events.
            </div>

            {roomEvents.length === 0 ? (
              <div className="empty-state" style={{ padding: 30 }}>
                <div className="empty-state__icon">📡</div>
                <div className="empty-state__title">No room events captured yet</div>
                <div className="empty-state__desc">Member join/leave events for joined rooms will append here.</div>
              </div>
            ) : (
              <div className="presence-events-list">
                {roomEvents.map(evt => (
                  <div key={evt.id} className="presence-event-item">
                    <span className={`presence-pill ${evt.type === 'user_joined' ? 'join' : evt.type === 'created' ? 'join' : 'leave'}`}>
                      {evt.type === 'user_joined' && '+ ROOM JOIN'}
                      {evt.type === 'user_left' && '- ROOM LEAVE'}
                      {evt.type === 'created' && '★ CREATED'}
                    </span>
                    <span className="presence-user mono">#{evt.room}</span>
                    <span className="presence-detail">User <strong>{evt.username}</strong> {evt.type.replace('_', ' ')}</span>
                    <span className="presence-time text-secondary">
                      {evt.timestamp.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' })}
                    </span>
                  </div>
                ))}
              </div>
            )}
          </div>
        )}
      </div>

      {/* Room Members Modal */}
      {selectedRoomForMembers && (
        <div className="modal-backdrop" onClick={() => setSelectedRoomForMembers(null)}>
          <div className="modal-card" onClick={e => e.stopPropagation()} style={{ maxWidth: 420 }}>
            <div className="modal-header" style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                <IcHash size={18} />
                <h3 style={{ margin: 0, fontSize: 15 }}>Members of #{selectedRoomForMembers}</h3>
              </div>
              <button className="btn-icon" onClick={() => setSelectedRoomForMembers(null)}>
                <IcClose size={14} />
              </button>
            </div>

            <div className="modal-body" style={{ padding: '16px 20px' }}>
              {!joinedRooms.includes(selectedRoomForMembers) ? (
                <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
                  <div className="alert-box warning">
                    <IcInfo size={14} />
                    <span>
                      <strong>Authorization Boundary:</strong> Per server policy, room member queries (<code>room_members_req</code>) require membership in the room. Non-members receive <code>ERR_NOT_IN_ROOM</code>.
                    </span>
                  </div>
                  <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', background: 'var(--canvas)', padding: '10px 14px', borderRadius: 6 }}>
                    <span style={{ fontSize: 12, color: 'var(--text-secondary)' }}>
                      Test live server rejection:
                    </span>
                    <button
                      id="test-unjoined-members-btn"
                      className="btn btn-secondary"
                      onClick={() => onRequestRoomMembers(selectedRoomForMembers)}
                      style={{ padding: '4px 10px', fontSize: 11 }}
                    >
                      Dispatch <code>room_members_req</code>
                    </button>
                  </div>
                  {roomError && (
                    <div className="alert-box error" style={{ margin: 0 }}>
                      <span className="mono" style={{ fontSize: 12 }}>{roomError}</span>
                    </div>
                  )}
                </div>
              ) : (
                <>
                  <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 12 }}>
                    <span style={{ fontSize: 12, color: 'var(--text-secondary)' }}>
                      Verified members in this room instance:
                    </span>
                    <button
                      className="btn-secondary"
                      onClick={() => onRequestRoomMembers(selectedRoomForMembers)}
                      style={{ padding: '2px 8px', fontSize: 11 }}
                    >
                      <IcRefresh size={11} /> Refresh
                    </button>
                  </div>

                  {(!roomMembers[selectedRoomForMembers] || roomMembers[selectedRoomForMembers].length === 0) ? (
                    <div style={{ padding: '20px 0', textAlign: 'center', color: 'var(--text-secondary)', fontSize: 13 }}>
                      Querying room members from server…
                    </div>
                  ) : (
                    <div style={{ display: 'flex', flexDirection: 'column', gap: 6, maxHeight: 240, overflowY: 'auto' }}>
                      {roomMembers[selectedRoomForMembers].map(m => (
                        <div key={m} className="user-pill-row">
                          <span style={{ width: 8, height: 8, borderRadius: '50%', background: 'var(--accent)', display: 'inline-block' }} />
                          <span className="mono" style={{ fontSize: 13, fontWeight: 500 }}>{m}</span>
                          {m === _username && <span className="chip accent" style={{ fontSize: 9 }}>You</span>}
                        </div>
                      ))}
                    </div>
                  )}
                </>
              )}
            </div>

            <div className="modal-footer" style={{ padding: '12px 20px', borderTop: '1px solid var(--border)', textAlign: 'right' }}>
              <button className="btn btn-secondary" onClick={() => setSelectedRoomForMembers(null)}>
                Close
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

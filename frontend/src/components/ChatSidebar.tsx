/** ChatSidebar.tsx — Context sidebar for the Chats view. */
import { useState } from 'react';
import type { ChatContext } from '../types';
import { IcSearch, IcPlus, IcHash } from './icons';

interface ChatSidebarProps {
  username: string;
  onlineUsers: string[];
  joinedRooms: string[];
  activeContext: ChatContext;
  onContextChange: (ctx: ChatContext) => void;
  onJoinRoom: (room: string) => void;
}

const initials = (name: string) => name.slice(0, 2).toUpperCase();

export default function ChatSidebar({
  username,
  onlineUsers,
  joinedRooms,
  activeContext,
  onContextChange,
  onJoinRoom,
}: ChatSidebarProps) {
  const [search, setSearch] = useState('');
  const [showJoinInput, setShowJoinInput] = useState(false);
  const [newRoom, setNewRoom] = useState('');

  const peers = onlineUsers.filter(u => u !== username);

  const filteredPeers = search
    ? peers.filter(u => u.toLowerCase().includes(search.toLowerCase()))
    : peers;
  const filteredRooms = search
    ? joinedRooms.filter(r => r.toLowerCase().includes(search.toLowerCase()))
    : joinedRooms;

  const isCtxGlobal = activeContext.type === 'global';
  const isCtxRoom = (r: string) => activeContext.type === 'room' && activeContext.room === r;
  const isCtxDm = (u: string) => activeContext.type === 'dm' && activeContext.peer === u;

  const handleJoinSubmit = () => {
    const r = newRoom.trim();
    if (r) { onJoinRoom(r); setNewRoom(''); setShowJoinInput(false); }
  };

  return (
    <aside className="sidebar" aria-label="Chat contexts">
      <div className="sidebar__header">
        <h2>Chats</h2>
      </div>

      <div className="sidebar__search relative">
        <span className="sidebar__search-icon"><IcSearch size={13} /></span>
        <input
          id="chat-search"
          type="text"
          placeholder="Search…"
          value={search}
          onChange={e => setSearch(e.target.value)}
          aria-label="Search chats"
        />
      </div>

      <div className="sidebar__list">
        {/* Global */}
        <div
          id="ctx-global"
          className={`sidebar__item${isCtxGlobal ? ' active' : ''}`}
          onClick={() => onContextChange({ type: 'global' })}
          role="button"
          tabIndex={0}
          onKeyDown={e => e.key === 'Enter' && onContextChange({ type: 'global' })}
        >
          <div className="sidebar__item-avatar" style={{ borderRadius: 8, background: '#E6F4F2', color: 'var(--accent)', fontSize: 16 }}>
            🌐
          </div>
          <div className="sidebar__item-info">
            <div className="sidebar__item-name">Global</div>
            <div className="sidebar__item-meta">Public broadcast</div>
          </div>
        </div>

        {/* Rooms */}
        <div className="sidebar__section-label"><h4>Rooms</h4></div>

        {filteredRooms.map(room => (
          <div
            key={room}
            id={`ctx-room-${room}`}
            className={`sidebar__item${isCtxRoom(room) ? ' active' : ''}`}
            onClick={() => onContextChange({ type: 'room', room })}
            role="button"
            tabIndex={0}
            onKeyDown={e => e.key === 'Enter' && onContextChange({ type: 'room', room })}
          >
            <div className="sidebar__item-avatar room"><IcHash size={13} /></div>
            <div className="sidebar__item-info">
              <div className="sidebar__item-name">#{room}</div>
            </div>
          </div>
        ))}

        {showJoinInput ? (
          <div style={{ padding: '6px 14px', display: 'flex', gap: 6 }}>
            <input
              id="quick-join-input"
              className="input-field"
              style={{ height: 30, fontSize: 12 }}
              placeholder="Room name…"
              value={newRoom}
              onChange={e => setNewRoom(e.target.value)}
              onKeyDown={e => e.key === 'Enter' && handleJoinSubmit()}
              autoFocus
              maxLength={32}
            />
            <button className="btn btn-primary" style={{ height: 30, padding: '0 10px', fontSize: 12 }} onClick={handleJoinSubmit}>
              Join
            </button>
          </div>
        ) : (
          <button
            id="quick-join-room-btn"
            className="sidebar__item btn-ghost"
            style={{ width: '100%', justifyContent: 'flex-start', borderRadius: 0, color: 'var(--text-secondary)', border: 'none', background: 'none', cursor: 'pointer', padding: '7px 16px', display: 'flex', alignItems: 'center', gap: 8, fontSize: 13 }}
            onClick={() => setShowJoinInput(true)}
          >
            <IcPlus size={13} /> Join a room…
          </button>
        )}

        {/* Direct Messages */}
        <div className="sidebar__section-label" style={{ marginTop: 8 }}><h4>Direct Messages</h4></div>

        {filteredPeers.length === 0 && (
          <div className="text-xs text-secondary" style={{ padding: '6px 16px' }}>No other users online</div>
        )}

        {filteredPeers.map(peer => (
          <div
            key={peer}
            id={`ctx-dm-${peer}`}
            className={`sidebar__item${isCtxDm(peer) ? ' active' : ''}`}
            onClick={() => onContextChange({ type: 'dm', peer })}
            role="button"
            tabIndex={0}
            onKeyDown={e => e.key === 'Enter' && onContextChange({ type: 'dm', peer })}
          >
            <div className="sidebar__item-avatar">{initials(peer)}</div>
            <div className="sidebar__item-info">
              <div className="sidebar__item-name">{peer}</div>
              <div className="sidebar__item-meta" style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
                <span style={{ width: 6, height: 6, borderRadius: '50%', background: 'var(--success)', display: 'inline-block' }} />
                Online
              </div>
            </div>
          </div>
        ))}
      </div>
    </aside>
  );
}

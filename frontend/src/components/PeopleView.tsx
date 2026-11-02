/**
 * PeopleView.tsx — Online users registry with real presence events stream.
 * Displays real online users and real-time join/leave presence notifications.
 */

import { useState, useMemo } from 'react';
import type { ChatContext, NavView, PresenceEvent } from '../types';
import { IcMessage, IcRefresh, IcSearch, IcUser } from './icons';

interface PeopleViewProps {
  username: string;
  onlineUsers: string[];
  totalUsers: number;
  userListPage?: number;
  userListTotalPages?: number;
  presenceEvents: PresenceEvent[];
  onRequestUsers: (page?: number) => void;
  onOpenDm: (peer: string) => void;
  onViewChange: (view: NavView) => void;
  onContextChange: (ctx: ChatContext) => void;
}

const initials = (name: string) => name.slice(0, 2).toUpperCase();

const AVATAR_COLORS = [
  { bg: '#D9EEEA', color: '#247D73' },
  { bg: '#EEE9F5', color: '#6B4E9E' },
  { bg: '#FFF4E6', color: '#A86D22' },
  { bg: '#E6F0FF', color: '#2451A8' },
  { bg: '#F5E6EE', color: '#A82462' },
];
const avatarColor = (name: string) => AVATAR_COLORS[name.charCodeAt(0) % AVATAR_COLORS.length];

export default function PeopleView({
  username,
  onlineUsers,
  totalUsers,
  userListPage = 1,
  userListTotalPages = 1,
  presenceEvents,
  onRequestUsers,
  onOpenDm,
  onViewChange: _onViewChange,
  onContextChange: _onContextChange,
}: PeopleViewProps) {
  const [search, setSearch] = useState('');
  const [activeTab, setActiveTab] = useState<'users' | 'events'>('users');

  const others = useMemo(() => {
    return onlineUsers
      .filter(u => u !== username)
      .filter(u => !search || u.toLowerCase().includes(search.toLowerCase()));
  }, [onlineUsers, username, search]);

  return (
    <div className="workspace">
      <div className="workspace__header">
        <div className="workspace__header-left">
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <IcUser size={18} />
            <h2>People</h2>
          </div>
          <span className="chip accent" title="Real online registered users">
            {totalUsers || onlineUsers.length} online
          </span>
        </div>
        <div className="workspace__header-right">
          <button
            id="refresh-people-btn"
            className="btn btn-ghost btn-icon"
            title="Refresh user list"
            onClick={() => onRequestUsers()}
            aria-label="Refresh user list"
          >
            <IcRefresh size={14} />
          </button>
        </div>
      </div>

      <div className="workspace__body" style={{ overflowY: 'auto', padding: '16px 24px' }}>
        {/* Navigation Tabs */}
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 16 }}>
          <div className="tab-group">
            <button
              className={`tab-btn ${activeTab === 'users' ? 'active' : ''}`}
              onClick={() => setActiveTab('users')}
            >
              Online Directory ({onlineUsers.length})
            </button>
            <button
              className={`tab-btn ${activeTab === 'events' ? 'active' : ''}`}
              onClick={() => setActiveTab('events')}
            >
              Presence Log ({presenceEvents.length})
            </button>
          </div>

          {activeTab === 'users' && (
            <div className="search-input-wrap" style={{ width: 220 }}>
              <IcSearch size={14} className="search-icon" />
              <input
                type="text"
                className="input-field"
                placeholder="Search people…"
                value={search}
                onChange={e => setSearch(e.target.value)}
                style={{ paddingLeft: 28, height: 32, fontSize: 12 }}
              />
            </div>
          )}
        </div>

        {activeTab === 'users' && (
          <>
            {/* Self Card */}
            <h4 style={{ marginBottom: 8, fontSize: 12, color: 'var(--text-secondary)', textTransform: 'uppercase', letterSpacing: '0.05em' }}>
              Your Session
            </h4>
            <div className="user-card" style={{ marginBottom: 20, cursor: 'default' }}>
              <div className="user-card__avatar" style={{ background: '#D9EEEA', color: '#247D73' }}>
                {initials(username)}
              </div>
              <div className="user-card__info">
                <div className="user-card__name">
                  {username}
                  <span className="chip accent" style={{ marginLeft: 8, fontSize: 10 }}>You</span>
                </div>
                <div className="user-card__status" style={{ display: 'flex', alignItems: 'center', gap: 5 }}>
                  <span style={{ width: 7, height: 7, borderRadius: '50%', background: 'var(--success, #247D73)', display: 'inline-block' }} />
                  Active TCP Connection
                </div>
              </div>
            </div>

            {/* Others List */}
            <h4 style={{ marginBottom: 8, fontSize: 12, color: 'var(--text-secondary)', textTransform: 'uppercase', letterSpacing: '0.05em' }}>
              Other Members ({others.length})
            </h4>

            {others.length === 0 && (
              <div className="empty-state" style={{ marginTop: 30, padding: 24 }}>
                <div className="empty-state__icon">👤</div>
                <div className="empty-state__title">
                  {search ? 'No matching users' : 'Nobody else online'}
                </div>
                <div className="empty-state__desc">
                  {search
                    ? `No online users matched "${search}".`
                    : 'When other browser or console clients connect, their presence will be tracked live.'}
                </div>
              </div>
            )}

            {others.map(user => {
              const col = avatarColor(user);
              return (
                <div key={user} className="user-card" id={`user-${user}`}>
                  <div className="user-card__avatar" style={{ background: col.bg, color: col.color }}>
                    {initials(user)}
                  </div>
                  <div className="user-card__info">
                    <div className="user-card__name">{user}</div>
                    <div className="user-card__status" style={{ display: 'flex', alignItems: 'center', gap: 5 }}>
                      <span style={{ width: 6, height: 6, borderRadius: '50%', background: 'var(--success, #247D73)', display: 'inline-block' }} />
                      Online
                    </div>
                  </div>
                  <div className="user-card__actions">
                    <button
                      id={`dm-btn-${user}`}
                      className="btn btn-secondary"
                      style={{ fontSize: 12, height: 30, gap: 5 }}
                      onClick={() => onOpenDm(user)}
                      aria-label={`Direct Message ${user}`}
                    >
                      <IcMessage size={12} /> Message
                    </button>
                  </div>
                </div>
              );
            })}

            {userListTotalPages > 1 && (
              <div style={{ display: 'flex', justifyContent: 'center', alignItems: 'center', gap: 12, marginTop: 16 }}>
                <button
                  className="btn btn-secondary"
                  disabled={userListPage <= 1}
                  onClick={() => onRequestUsers(userListPage - 1)}
                  style={{ height: 28, fontSize: 11 }}
                >
                  ← Previous
                </button>
                <span style={{ fontSize: 12, color: 'var(--text-secondary)' }}>
                  Page {userListPage} of {userListTotalPages}
                </span>
                <button
                  className="btn btn-secondary"
                  disabled={userListPage >= userListTotalPages}
                  onClick={() => onRequestUsers(userListPage + 1)}
                  style={{ height: 28, fontSize: 11 }}
                >
                  Next →
                </button>
              </div>
            )}
          </>
        )}

        {/* Real-time Presence Events Log */}
        {activeTab === 'events' && (
          <div className="presence-log-panel">
            <div style={{ marginBottom: 12, fontSize: 12, color: 'var(--text-secondary)' }}>
              Real-time audit log of <code>system_event</code> and <code>user_joined</code> / <code>user_left</code> frames received from the server.
            </div>

            {presenceEvents.length === 0 ? (
              <div className="empty-state" style={{ padding: 30 }}>
                <div className="empty-state__icon">📡</div>
                <div className="empty-state__title">No presence events yet</div>
                <div className="empty-state__desc">Join and leave events from other connections will append here.</div>
              </div>
            ) : (
              <div className="presence-events-list">
                {presenceEvents.map(evt => {
                  const isJoin = evt.type === 'join';
                  return (
                    <div key={evt.id} className="presence-event-item">
                      <span className={`presence-pill ${isJoin ? 'join' : 'leave'}`}>
                        {isJoin ? '+ JOIN' : '- LEAVE'}
                      </span>
                      <span className="presence-user mono">{evt.username}</span>
                      <span className="presence-detail">{evt.detail}</span>
                      <span className="presence-time text-secondary">
                        {evt.timestamp.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' })}
                      </span>
                    </div>
                  );
                })}
              </div>
            )}
          </div>
        )}
      </div>
    </div>
  );
}

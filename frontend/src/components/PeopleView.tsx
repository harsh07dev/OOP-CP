/**
 * PeopleView.tsx — Online users list with DM navigation.
 */

import type { ChatContext, NavView } from '../types';
import { IcMessage, IcRefresh } from './icons';

interface PeopleViewProps {
  username: string;
  onlineUsers: string[];
  onRequestUsers: () => void;
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
  onRequestUsers,
  onViewChange,
  onContextChange,
}: PeopleViewProps) {
  const others = onlineUsers.filter(u => u !== username);

  return (
    <div className="workspace">
      <div className="workspace__header">
        <div className="workspace__header-left">
          <h2>People</h2>
          <span className="chip accent">{onlineUsers.length} online</span>
        </div>
        <div className="workspace__header-right">
          <button
            id="refresh-people-btn"
            className="btn btn-ghost btn-icon"
            title="Refresh user list"
            onClick={onRequestUsers}
            aria-label="Refresh user list"
          >
            <IcRefresh size={14} />
          </button>
        </div>
      </div>

      <div className="workspace__body" style={{ overflowY: 'auto', padding: '16px 20px' }}>
        <h4 style={{ marginBottom: 8 }}>You</h4>
        <div className="user-card" style={{ marginBottom: 16, cursor: 'default' }}>
          <div className="user-card__avatar" style={{ background: '#D9EEEA', color: '#247D73' }}>
            {initials(username)}
          </div>
          <div className="user-card__info">
            <div className="user-card__name">
              {username}
              <span className="chip accent" style={{ marginLeft: 8, fontSize: 10 }}>You</span>
            </div>
            <div className="user-card__status">Connected</div>
          </div>
        </div>

        <h4 style={{ marginBottom: 8 }}>Online ({others.length})</h4>

        {others.length === 0 && (
          <div className="empty-state" style={{ marginTop: 40 }}>
            <div className="empty-state__icon">👤</div>
            <div className="empty-state__title">Nobody else online</div>
            <div className="empty-state__desc">Other users will appear here when they connect.</div>
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
                <div className="user-card__status" style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
                  <span style={{ width: 6, height: 6, borderRadius: '50%', background: 'var(--success)', display: 'inline-block' }} />
                  Online
                </div>
              </div>
              <div className="user-card__actions">
                <button
                  id={`dm-btn-${user}`}
                  className="btn btn-secondary"
                  style={{ fontSize: 12, height: 30, gap: 5 }}
                  onClick={() => {
                    onContextChange({ type: 'dm', peer: user });
                    onViewChange('chats');
                  }}
                  aria-label={`Message ${user}`}
                >
                  <IcMessage size={12} /> Message
                </button>
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}

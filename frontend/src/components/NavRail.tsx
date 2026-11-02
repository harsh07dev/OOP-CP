/** NavRail.tsx — Compact vertical navigation rail. */
import React from 'react';
import type { NavView } from '../types';
import {
  IcChat,
  IcPeople,
  IcRoom,
  IcHistory,
  IcLab,
  IcLogo,
  IcLogout,
} from './icons';
import type { ConnectionStatus } from '../types';

interface NavRailProps {
  activeView: NavView;
  onViewChange: (v: NavView) => void;
  username: string | null;
  status: ConnectionStatus;
  onDisconnect: () => void;
  unreadCount?: number;
}

const NAV_ITEMS: { view: NavView; label: string; Icon: React.FC<{ size?: number }> }[] = [
  { view: 'chats',   label: 'Chats',      Icon: IcChat },
  { view: 'people',  label: 'People',     Icon: IcPeople },
  { view: 'rooms',   label: 'Rooms',      Icon: IcRoom },
  { view: 'history', label: 'History',    Icon: IcHistory },
  { view: 'lab',     label: 'System Lab', Icon: IcLab },
];

const initials = (name: string) =>
  name.slice(0, 2).toUpperCase();

export default function NavRail({
  activeView,
  onViewChange,
  username,
  status,
  onDisconnect,
}: NavRailProps) {
  return (
    <nav className="nav-rail" aria-label="Main navigation">
      {/* Logo */}
      <div className="nav-rail__logo" aria-label="ChatDesk">
        <IcLogo size={20} />
      </div>

      {/* Nav items */}
      <div className="nav-rail__items">
        {NAV_ITEMS.map(({ view, label, Icon }) => (
          <button
            key={view}
            id={`nav-${view}`}
            className={`nav-rail__item${activeView === view ? ' active' : ''}`}
            data-tooltip={label}
            aria-label={label}
            aria-current={activeView === view ? 'page' : undefined}
            onClick={() => onViewChange(view)}
            title={label}
          >
            <Icon size={18} />
          </button>
        ))}
      </div>

      {/* Bottom: user avatar + status */}
      <div className="nav-rail__bottom">
        {username && (
          <button
            id="disconnect-btn"
            className="nav-rail__item"
            data-tooltip="Disconnect"
            aria-label="Disconnect"
            onClick={onDisconnect}
            title="Disconnect"
          >
            <IcLogout size={16} />
          </button>
        )}
        <div
          className="nav-rail__avatar"
          title={username ?? 'Not connected'}
          aria-label={`Signed in as ${username ?? 'nobody'}`}
        >
          {username ? initials(username) : '?'}
        </div>
        <div
          className={`nav-rail__status-dot ${status === 'connected' ? 'online' : status === 'connecting' ? 'connecting' : 'offline'}`}
          title={status}
          aria-label={`Connection: ${status}`}
        />
      </div>
    </nav>
  );
}

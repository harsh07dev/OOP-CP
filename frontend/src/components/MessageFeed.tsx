/**
 * MessageFeed.tsx — Message list with accurate delivery state display.
 *
 * Status legend:
 *   pending    → clock icon, muted — awaiting server response
 *   accepted   → single check — server stored + broadcast (public / room)
 *   dispatched → single check — server dispatched to recipient socket (DM)
 *   failed     → ✕ red — server rejected or WS disconnected before ACK
 *   (no "delivered" or "read" states — not provided by this protocol)
 */
import { Fragment, useEffect, useRef } from 'react';
import type { ChatMessage, MessageStatus } from '../types';
import { IcFlightRecorder } from './icons';

interface MessageFeedProps {
  messages: ChatMessage[];
  currentUser: string;
  onSelectMessage?: (msg: ChatMessage) => void;
}

const initials = (name: string) => name.slice(0, 2).toUpperCase();

const formatTime = (d: Date) =>
  d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });

const formatDateLabel = (d: Date): string => {
  const today = new Date();
  const yesterday = new Date(today);
  yesterday.setDate(today.getDate() - 1);
  if (d.toDateString() === today.toDateString()) return 'Today';
  if (d.toDateString() === yesterday.toDateString()) return 'Yesterday';
  return d.toLocaleDateString([], { weekday: 'long', month: 'short', day: 'numeric' });
};

function groupByDate(msgs: ChatMessage[]) {
  const groups: { label: string; msgs: ChatMessage[] }[] = [];
  let curLabel = '';
  let curGroup: ChatMessage[] = [];
  for (const m of msgs) {
    const label = formatDateLabel(m.timestamp);
    if (label !== curLabel) {
      if (curGroup.length > 0) groups.push({ label: curLabel, msgs: curGroup });
      curLabel = label;
      curGroup = [m];
    } else {
      curGroup.push(m);
    }
  }
  if (curGroup.length > 0) groups.push({ label: curLabel, msgs: curGroup });
  return groups;
}

/* ── Status indicator ──────────────────────────────────────────────── */
function StatusBadge({ status, kind: _kind }: { status?: MessageStatus; kind: string }) {
  if (!status || status === 'pending') {
    return <span style={{ color: 'var(--text-secondary)', fontSize: 10 }}>⏱ pending</span>;
  }
  if (status === 'failed') {
    return <span style={{ color: 'var(--error)', fontSize: 10 }}>✕ failed</span>;
  }
  if (status === 'accepted') {
    // Server stored it; other clients receive the broadcast
    return <span style={{ color: 'var(--accent)', fontSize: 10 }}>✓ accepted</span>;
  }
  if (status === 'dispatched') {
    // DM: server dispatched to recipient socket (not a read receipt)
    return <span style={{ color: 'var(--accent)', fontSize: 10, fontStyle: 'italic' }} title="Server dispatched to recipient (not a read receipt)">✓ sent</span>;
  }
  return null;
}

export default function MessageFeed({ messages, currentUser: _currentUser, onSelectMessage }: MessageFeedProps) {
  const feedRef = useRef<HTMLDivElement>(null);
  const bottomRef = useRef<HTMLDivElement>(null);
  const nearBottomRef = useRef(true);

  const onScroll = () => {
    const el = feedRef.current;
    if (!el) return;
    nearBottomRef.current = (el.scrollHeight - el.scrollTop - el.clientHeight) < 120;
  };

  useEffect(() => {
    if (nearBottomRef.current) {
      bottomRef.current?.scrollIntoView({ behavior: 'smooth' });
    }
  }, [messages]);

  if (messages.length === 0) {
    return (
      <div className="message-feed" style={{ alignItems: 'center', justifyContent: 'center' }}>
        <div className="empty-state">
          <div className="empty-state__icon">💬</div>
          <div className="empty-state__title">No messages yet</div>
          <div className="empty-state__desc">
            Start the conversation or join a room from the sidebar.
          </div>
        </div>
      </div>
    );
  }

  const groups = groupByDate(messages);

  return (
    <div
      className="message-feed"
      ref={feedRef}
      onScroll={onScroll}
      role="log"
      aria-live="polite"
      aria-label="Chat messages"
    >
      {groups.map(({ label, msgs }) => (
        <Fragment key={label}>
          {/* Date divider */}
          <div className="date-divider" aria-label={label}>
            <div className="date-divider__line" />
            <span className="date-divider__label">{label}</span>
            <div className="date-divider__line" />
          </div>

          {msgs.map(msg => {
            const isOwn = msg.own === true;
            const isSystem = msg.kind === 'system';
            const isError = msg.kind === 'error';
            const isPrivate = msg.kind === 'private';
            const isRoom = msg.kind === 'room';

            /* System / error rows — centered */
            if (isSystem || isError) {
              return (
                <div key={msg.id} className="message-row" style={{ justifyContent: 'center' }}>
                  <span className={`message-bubble ${isError ? 'error-msg' : 'system'}`}
                        style={{ maxWidth: '90%', textAlign: 'center', boxShadow: 'none', border: isSystem ? 'none' : undefined }}>
                    {msg.body}
                  </span>
                </div>
              );
            }

            return (
              <div
                key={msg.id}
                className={`message-row${isOwn ? ' own' : ''}`}
                role="listitem"
              >
                {/* Avatar — only for incoming */}
                {!isOwn && (
                  <div className="message-avatar" title={msg.sender}>
                    {initials(msg.sender)}
                  </div>
                )}

                <div className="message-content">
                  {/* Header (sender + tags + time) — incoming only */}
                  {!isOwn && (
                    <div className="message-header">
                      <span className="message-sender">{msg.sender}</span>
                      {isPrivate && <span className="chip" style={{ fontSize: 10, padding: '1px 5px' }}>Private</span>}
                      {isRoom && msg.room && <span className="chip accent" style={{ fontSize: 10, padding: '1px 5px' }}>#{msg.room}</span>}
                      <span className="message-time">{formatTime(msg.timestamp)}</span>
                    </div>
                  )}

                  {/* Bubble & Actions */}
                  <div className="message-bubble-wrap">
                    <div className={`message-bubble${isPrivate && !isOwn ? ' private' : ''}`}>
                      {msg.body}
                    </div>
                    {onSelectMessage && (
                      <button
                        className="btn-msg-inspect"
                        onClick={(e) => {
                          e.stopPropagation();
                          onSelectMessage(msg);
                        }}
                        title="Open in Message Flight Recorder"
                        aria-label="Inspect message"
                      >
                        <IcFlightRecorder size={12} />
                      </button>
                    )}
                  </div>

                  {/* Own message: time + status (no simulated states) */}
                  {isOwn && (
                    <div className="message-status" style={{ marginRight: 2, gap: 6 }}>
                      <span style={{ fontSize: 11, color: 'var(--text-secondary)' }}>{formatTime(msg.timestamp)}</span>
                      <StatusBadge status={msg.status} kind={msg.kind} />
                    </div>
                  )}
                </div>
              </div>
            );
          })}
        </Fragment>
      ))}
      <div ref={bottomRef} aria-hidden="true" />
    </div>
  );
}

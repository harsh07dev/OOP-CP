/**
 * ChatsView.tsx — Main chat workspace.
 * Correctly routes messages to public / room / DM contexts.
 * Supports Message Flight Recorder inspection for any selected message.
 */
import { useState, useCallback } from 'react';
import type { ChatContext, ChatMessage } from '../types';
import ChatSidebar from './ChatSidebar';
import MessageFeed from './MessageFeed';
import ComposeBar from './ComposeBar';
import FlightRecorder from './FlightRecorder';
import { IcHash, IcUser, IcRefresh, IcFlightRecorder } from './icons';
import type { ChatSocketAPI } from '../hooks/useChatSocket';

interface ChatsViewProps {
  api: ChatSocketAPI;
  activeContext: ChatContext;
  onContextChange: (ctx: ChatContext) => void;
}

const contextTitle = (ctx: ChatContext): string => {
  switch (ctx.type) {
    case 'global': return 'Global';
    case 'room':   return `#${ctx.room}`;
    case 'dm':     return ctx.peer;
  }
};

/**
 * Filter messages for the active context.
 * System/error messages are always shown.
 */
function filterMessages(messages: ChatMessage[], ctx: ChatContext, _me: string): ChatMessage[] {
  return messages.filter(m => {
    if (m.kind === 'system' || m.kind === 'error') return true;

    switch (ctx.type) {
      case 'global':
        // Public broadcast (not in a room)
        return m.kind === 'public' && !m.room;
      case 'room':
        // Room messages for this specific room
        return m.kind === 'room' && m.room === ctx.room;
      case 'dm': {
        // Private messages between me and the peer
        if (m.kind !== 'private') return false;
        if (m.own) return m.target === ctx.peer;              // sent by me to peer
        return m.sender === ctx.peer;                         // received from peer
      }
    }
  });
}

export default function ChatsView({ api, activeContext, onContextChange }: ChatsViewProps) {
  const { username, onlineUsers, joinedRooms, messages } = api;
  const [selectedMessage, setSelectedMessage] = useState<ChatMessage | null>(null);

  const visibleMessages = filterMessages(messages, activeContext, username ?? '');

  const handleSend = useCallback((body: string) => {
    switch (activeContext.type) {
      case 'global':
        api.sendPublic(body);
        break;
      case 'room':
        api.sendRoomMessage(activeContext.room, body);
        break;
      case 'dm':
        api.sendPrivate(activeContext.peer, body);
        break;
    }
  }, [activeContext, api]);

  const handleJoinRoom = useCallback((room: string) => {
    api.joinRoom(room);
    onContextChange({ type: 'room', room });
  }, [api, onContextChange]);

  const contextSubtitle = (): string => {
    switch (activeContext.type) {
      case 'global': return `${onlineUsers.length} online`;
      case 'room':   return `Room`;
      case 'dm':     return 'Direct message';
    }
  };

  return (
    <>
      <ChatSidebar
        username={username ?? ''}
        onlineUsers={onlineUsers}
        joinedRooms={joinedRooms}
        activeContext={activeContext}
        onContextChange={onContextChange}
        onJoinRoom={handleJoinRoom}
      />

      <div className="workspace">
        <div className="workspace__header">
          <div className="workspace__header-left">
            <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
              {activeContext.type === 'room'   && <IcHash size={16} />}
              {activeContext.type === 'dm'     && <IcUser size={16} />}
              {activeContext.type === 'global' && <span>🌐</span>}
              <h2>{contextTitle(activeContext)}</h2>
            </div>
            <span className="text-xs text-secondary">{contextSubtitle()}</span>
          </div>
          <div className="workspace__header-right">
            {visibleMessages.length > 0 && (
              <button
                className={`btn btn-ghost btn-icon ${selectedMessage ? 'btn-active' : ''}`}
                title="Inspect latest message in Flight Recorder"
                onClick={() => setSelectedMessage(selectedMessage ? null : visibleMessages[visibleMessages.length - 1])}
                aria-label="Toggle flight recorder"
              >
                <IcFlightRecorder size={15} />
              </button>
            )}
            <button
              id="refresh-users-btn"
              className="btn btn-ghost btn-icon"
              title="Refresh user list"
              onClick={() => api.requestUserList()}
              aria-label="Refresh user list"
            >
              <IcRefresh size={14} />
            </button>
          </div>
        </div>

        <div className="workspace-main-row" style={{ display: 'flex', flex: 1, minHeight: 0, position: 'relative' }}>
          <div style={{ display: 'flex', flexDirection: 'column', flex: 1, minWidth: 0, height: '100%' }}>
            <MessageFeed
              messages={visibleMessages}
              currentUser={username ?? ''}
              onSelectMessage={setSelectedMessage}
            />
            <ComposeBar context={activeContext} onSend={handleSend} />
          </div>

          {selectedMessage && (
            <FlightRecorder
              message={selectedMessage}
              onClose={() => setSelectedMessage(null)}
            />
          )}
        </div>
      </div>
    </>
  );
}

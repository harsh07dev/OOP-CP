/**
 * App.tsx — Root application. Manages navigation, context state,
 * and wires the protocol hook to all views.
 */
import { useState, useCallback, useEffect, useRef } from 'react';
import type { NavView, ChatContext } from './types';
import { useChatSocket } from './hooks/useChatSocket';

import NavRail from './components/NavRail';
import LoginModal from './components/LoginModal';
import ChatsView from './components/ChatsView';
import PeopleView from './components/PeopleView';
import RoomsView from './components/RoomsView';
import HistoryView from './components/HistoryView';
import LabView from './components/LabView';

export default function App() {
  const api = useChatSocket();
  const [activeView, setActiveView] = useState<NavView>('chats');
  const [activeContext, setActiveContext] = useState<ChatContext>({ type: 'global' });

  // Track the last registration error message to surface in the login modal
  const [loginError, setLoginError] = useState<string | null>(null);

  // Watch for error messages that indicate registration failure
  const prevMsgLen = useRef(0);
  useEffect(() => {
    const newMsgs = api.messages.slice(prevMsgLen.current);
    prevMsgLen.current = api.messages.length;
    for (const m of newMsgs) {
      if (m.kind === 'error' && !api.username) {
        setLoginError(m.body);
      }
    }
  }, [api.messages, api.username]);

  // After login, fetch initial state
  useEffect(() => {
    if (api.username) {
      api.requestUserList();
      api.requestRoomList();
    }
  }, [api.username]); // eslint-disable-line react-hooks/exhaustive-deps

  const handleConnect = useCallback((bridgeUrl: string, username: string) => {
    setLoginError(null);
    api.connect(bridgeUrl, username);
  }, [api]);

  useEffect(() => {
    ((window as unknown) as Record<string, unknown>).__chatSocketAPI = api;
  }, [api]);

  const showLogin = api.status !== 'connected' || !api.username;

  return (
    <div className="app-shell">
      {showLogin && (
        <LoginModal
          status={api.status}
          error={api.username ? null : loginError}
          onConnect={handleConnect}
        />
      )}

      <NavRail
        activeView={activeView}
        onViewChange={setActiveView}
        username={api.username}
        status={api.status}
        onDisconnect={api.disconnect}
      />

      {activeView === 'chats' && (
        <ChatsView
          api={api}
          activeContext={activeContext}
          onContextChange={setActiveContext}
        />
      )}

      {activeView === 'people' && (
        <PeopleView
          username={api.username ?? ''}
          onlineUsers={api.onlineUsers}
          totalUsers={api.totalUsers}
          presenceEvents={api.presenceEvents}
          onRequestUsers={api.requestUserList}
          onOpenDm={(peer) => {
            setActiveContext({ type: 'dm', peer });
            setActiveView('chats');
          }}
          onViewChange={setActiveView}
          onContextChange={(ctx) => {
            setActiveContext(ctx);
            setActiveView('chats');
          }}
        />
      )}

      {activeView === 'rooms' && (
        <RoomsView
          username={api.username ?? ''}
          joinedRooms={api.joinedRooms}
          discoveredRooms={api.discoveredRooms}
          roomMembers={api.roomMembers}
          roomEvents={api.roomEvents}
          onJoinRoom={api.joinRoom}
          onCreateRoom={api.createRoom}
          onLeaveRoom={api.leaveRoom}
          onRequestRooms={api.requestRoomList}
          onRequestRoomMembers={api.requestRoomMembers}
          onContextChange={setActiveContext}
          onViewChange={(v) => setActiveView(v)}
        />
      )}

      {activeView === 'history' && (
        <HistoryView
          joinedRooms={api.joinedRooms}
          onlineUsers={api.onlineUsers}
          historyEntries={api.historyEntries}
          historyHasMore={api.historyHasMore}
          historyCount={api.historyCount}
          onRequestHistory={api.requestHistory}
        />
      )}

      {activeView === 'lab' && (
        <LabView
          status={api.status}
          username={api.username}
          onlineUsers={api.onlineUsers}
          joinedRooms={api.joinedRooms}
          messages={api.messages}
          protocolFrames={api.protocolFrames}
          connectedAt={api.connectedAt}
          inboundCount={api.inboundCount}
          outboundCount={api.outboundCount}
          inboundBytes={api.inboundBytes}
          outboundBytes={api.outboundBytes}
          onRequestUsers={api.requestUserList}
          onRequestRooms={api.requestRoomList}
          onRequestHistory={() => api.requestHistory('public', { limit: 20 })}
          onClearMessages={api.clearMessages}
          onClearFrames={api.clearProtocolFrames}
          onSendRaw={api.sendRawFrame}
        />
      )}
    </div>
  );
}

/**
 * LoginModal.tsx — Connect + register overlay.
 * Username validation mirrors server rules: 1–32 chars, [a-zA-Z0-9_-].
 */
import { useState, useRef, type KeyboardEvent, useEffect } from 'react';
import type { ConnectionStatus } from '../types';
import { IcLogo } from './icons';

interface LoginModalProps {
  status: ConnectionStatus;
  error: string | null;        // last registration error from server
  onConnect: (bridgeUrl: string, username: string) => void;
}

const USERNAME_PATTERN = /^[a-zA-Z0-9_-]{1,32}$/;

export default function LoginModal({ status, error, onConnect }: LoginModalProps) {
  const [bridgeUrl, setBridgeUrl] = useState(
    `ws://${window.location.hostname}:8081`
  );
  const [username, setUsername] = useState('');
  const [localError, setLocalError] = useState('');
  const usernameRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    usernameRef.current?.focus();
  }, []);

  const isConnecting = status === 'connecting';
  const serverError = error && !localError ? error : null;

  const validate = (uname: string): string => {
    if (!uname) return 'Username is required';
    if (!USERNAME_PATTERN.test(uname)) {
      if (uname.length > 32) return 'Maximum 32 characters';
      return 'Only letters, digits, _ and - are allowed';
    }
    return '';
  };

  const handleSubmit = () => {
    const trimmed = username.trim();
    const err = validate(trimmed);
    if (err) { setLocalError(err); return; }
    if (!bridgeUrl.trim().startsWith('ws')) {
      setLocalError('Bridge URL must start with ws:// or wss://');
      return;
    }
    setLocalError('');
    onConnect(bridgeUrl.trim(), trimmed);
  };

  const handleKey = (e: KeyboardEvent<HTMLInputElement>) => {
    if (e.key === 'Enter') handleSubmit();
  };

  const displayError = localError || serverError;

  return (
    <div className="modal-overlay">
      <div className="modal-card">
        <div className="modal-card__logo">
          <IcLogo size={24} />
        </div>

        <h1 className="modal-card__title">ChatDesk</h1>
        <p className="modal-card__subtitle">
          Connect to your TCP Chat System via the WebSocket bridge.
        </p>

        <div className="modal-card__form">
          <div className="form-group">
            <label className="form-label" htmlFor="bridge-url">Bridge URL</label>
            <input
              id="bridge-url"
              className="input-field"
              type="text"
              value={bridgeUrl}
              onChange={(e) => setBridgeUrl(e.target.value)}
              onKeyDown={handleKey}
              disabled={isConnecting}
              spellCheck={false}
              autoComplete="off"
            />
          </div>

          <div className="form-group">
            <label className="form-label" htmlFor="login-username">Username</label>
            <input
              id="login-username"
              ref={usernameRef}
              className="input-field"
              type="text"
              placeholder="Letters, digits, _ or - (max 32)"
              value={username}
              onChange={(e) => { setUsername(e.target.value); setLocalError(''); }}
              onKeyDown={handleKey}
              disabled={isConnecting}
              autoComplete="off"
              maxLength={32}
            />
            {displayError && (
              <span className="form-error" role="alert">{displayError}</span>
            )}
          </div>

          <button
            id="connect-btn"
            className="btn btn-primary w-full"
            style={{ height: 40, marginTop: 4 }}
            onClick={handleSubmit}
            disabled={isConnecting}
          >
            {isConnecting ? 'Connecting…' : 'Connect & Sign In'}
          </button>

          <p className="text-xs text-secondary" style={{ textAlign: 'center', marginTop: 4 }}>
            Requires C++ server on :8080 + bridge on :8081
          </p>
        </div>
      </div>
    </div>
  );
}

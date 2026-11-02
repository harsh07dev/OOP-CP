/**
 * FlightRecorder.tsx — Message Flight Recorder Inspector.
 * Inspects a selected message's correlation ID, server message ID,
 * protocol version, type, routing, ACK state, and genuine persistence status.
 */

import { useState } from 'react';
import type { ChatMessage } from '../types';
import { IcClose, IcFlightRecorder, IcCheck, IcInfo } from './icons';

interface FlightRecorderProps {
  message: ChatMessage | null;
  onClose: () => void;
}

export default function FlightRecorder({ message, onClose }: FlightRecorderProps) {
  const [copied, setCopied] = useState(false);

  if (!message) return null;

  const {
    id,
    msgId,
    correlationId,
    protocolVersion = 1,
    rawType,
    kind,
    sender,
    target,
    room,
    body,
    timestamp,
    own,
    status,
    persistence,
    rawPayload,
  } = message;

  // Generate a formatted JSON wire preview if rawPayload is missing
  let formattedWire = rawPayload ?? '';
  if (!formattedWire) {
    const syntheticObj: Record<string, unknown> = {
      version: protocolVersion,
      type: rawType ?? (kind === 'public' ? (own ? 'msg_send' : 'msg_broadcast') : kind),
      content: body,
      timestamp: Math.floor(timestamp.getTime() / 1000),
    };
    if (correlationId) syntheticObj.id = correlationId;
    if (msgId) syntheticObj.msg_id = msgId;
    if (target) syntheticObj.to = target;
    if (room) syntheticObj.room = room;
    if (!own) syntheticObj.from = sender;
    formattedWire = JSON.stringify(syntheticObj, null, 2);
  } else {
    try {
      formattedWire = JSON.stringify(JSON.parse(rawPayload!), null, 2);
    } catch {
      // keep raw string
    }
  }

  const handleCopy = () => {
    navigator.clipboard.writeText(formattedWire).then(() => {
      setCopied(true);
      setTimeout(() => setCopied(false), 1800);
    });
  };

  // Determine accurate persistence label based on backend invariants
  const persistenceLabel = (() => {
    if (persistence === 'confirmed' || msgId) {
      return {
        text: 'Committed to SQLite (Transaction Verified)',
        tone: 'var(--accent)',
        detail: 'The C++ backend guarantees commit-before-ACK. SQLite WAL committed before broadcast/ACK.',
      };
    }
    if (persistence === 'pending' || status === 'pending') {
      return {
        text: 'Pending Database Commit',
        tone: 'var(--pending, #A86D22)',
        detail: 'Frame queued or in transit; awaiting server ACK after SQLite transaction commit.',
      };
    }
    if (persistence === 'failed' || status === 'failed') {
      return {
        text: 'Storage Failed / Rejected',
        tone: 'var(--error, #B54747)',
        detail: 'Request rejected, oversized, or server encountered a database error.',
      };
    }
    return {
      text: 'Ephemeral (Not Persisted)',
      tone: 'var(--text-secondary)',
      detail: 'Presence and administrative events are memory-only and not stored in message history.',
    };
  })();

  // Determine accurate ACK label
  const ackLabel = (() => {
    if (own) {
      if (status === 'accepted') return '✓ msg_ack / room_msg_ack (Server Accepted & Stored)';
      if (status === 'dispatched') return '✓ msg_direct_ack (Server Dispatched to Peer Socket)';
      if (status === 'pending') return '⏱ Awaiting Server ACK';
      if (status === 'failed') return '✕ Failed / Rejected by Server';
    }
    return '← Inbound Frame from Server';
  })();

  return (
    <aside className="flight-recorder-panel" aria-label="Message Flight Recorder">
      <div className="flight-recorder-header">
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <span style={{ color: 'var(--accent)' }}><IcFlightRecorder size={18} /></span>
          <div>
            <h3 style={{ margin: 0, fontSize: 14, fontWeight: 600 }}>Message Flight Recorder</h3>
            <span style={{ fontSize: 11, color: 'var(--text-secondary)' }}>Protocol Inspection & Diagnostics</span>
          </div>
        </div>
        <button
          className="btn-icon"
          onClick={onClose}
          title="Close flight recorder"
          aria-label="Close"
          style={{ padding: 4 }}
        >
          <IcClose size={16} />
        </button>
      </div>

      <div className="flight-recorder-body">
        {/* Content Preview */}
        <div className="fr-section">
          <span className="fr-label">Message Payload Content</span>
          <div className="fr-content-preview">
            &ldquo;{body}&rdquo;
          </div>
        </div>

        {/* Diagnostic Metadata Grid */}
        <div className="fr-grid">
          <div className="fr-item">
            <span className="fr-label">Server Message ID (`msg_id`)</span>
            <span className="fr-value mono" style={{ color: msgId ? 'var(--accent)' : 'var(--text-secondary)' }}>
              {msgId ?? '(Unassigned / Pending)'}
            </span>
          </div>

          <div className="fr-item">
            <span className="fr-label">Request Correlation ID (`id`)</span>
            <span className="fr-value mono">
              {correlationId ?? '(None — Inbound Event)'}
            </span>
          </div>

          <div className="fr-item">
            <span className="fr-label">Protocol Version</span>
            <span className="fr-value">
              v{protocolVersion} (Structured JSON, LF framed)
            </span>
          </div>

          <div className="fr-item">
            <span className="fr-label">Contract Type</span>
            <span className="fr-value mono">
              {rawType ?? (kind === 'public' ? 'msg_send/msg_broadcast' : kind)}
            </span>
          </div>

          <div className="fr-item">
            <span className="fr-label">Routing & Channel</span>
            <span className="fr-value">
              {kind === 'public' && 'Global Broadcast'}
              {kind === 'room' && `#${room ?? 'unknown'}`}
              {kind === 'private' && `Direct Message → ${target ?? sender}`}
              {kind === 'system' && 'System Event'}
              {kind === 'error' && 'Error Frame'}
            </span>
          </div>

          <div className="fr-item">
            <span className="fr-label">Sender & Direction</span>
            <span className="fr-value">
              {own ? `Outbound (Self: ${sender})` : `Inbound (From: ${sender})`}
            </span>
          </div>

          <div className="fr-item full-width">
            <span className="fr-label">ACK & Dispatch State</span>
            <span className="fr-value" style={{ fontWeight: 500 }}>
              {ackLabel}
            </span>
            <span style={{ fontSize: 10, color: 'var(--text-secondary)', marginTop: 2, display: 'block' }}>
              Note: Server ACK confirms acceptance & DB commit. It is not an end-to-end read receipt.
            </span>
          </div>

          <div className="fr-item full-width">
            <span className="fr-label">Persistence Status</span>
            <span className="fr-value" style={{ color: persistenceLabel.tone, fontWeight: 600 }}>
              {persistenceLabel.text}
            </span>
            <span style={{ fontSize: 10, color: 'var(--text-secondary)', marginTop: 2, display: 'block' }}>
              {persistenceLabel.detail}
            </span>
          </div>

          <div className="fr-item full-width">
            <span className="fr-label">Timestamp</span>
            <span className="fr-value mono" style={{ fontSize: 11 }}>
              {timestamp.toISOString()} ({Math.floor(timestamp.getTime() / 1000)} unix sec)
            </span>
          </div>
        </div>

        {/* Raw Wire JSON */}
        <div className="fr-section">
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 6 }}>
            <span className="fr-label" style={{ margin: 0 }}>Wire Frame Representation (v1 JSON)</span>
            <button className="btn-secondary" onClick={handleCopy} style={{ padding: '2px 8px', fontSize: 11 }}>
              {copied ? (
                <>
                  <IcCheck size={12} /> Copied
                </>
              ) : (
                'Copy JSON'
              )}
            </button>
          </div>
          <pre className="fr-wire-box mono">{formattedWire}</pre>
        </div>

        <div className="fr-footer-note">
          <IcInfo size={13} />
          <span>Local UI ID: <code>{id}</code>. Wire framing strictly bounded at 2048 bytes with <code>\n</code> delimiter.</span>
        </div>
      </div>
    </aside>
  );
}

/**
 * ProtocolLens.tsx — Transparent v0/v1 Protocol Packet Inspector.
 * Inspects real wire framing, message types, correlation IDs, and packet sizes
 * with optional privacy redaction to protect sensitive conversation content.
 */

import { useState, useMemo } from 'react';
import type { ProtocolFrame } from '../types';
import { IcSearch, IcRefresh, IcShield, IcCheck } from './icons';

interface ProtocolLensProps {
  frames: ProtocolFrame[];
  onClear: () => void;
}

export default function ProtocolLens({ frames, onClear }: ProtocolLensProps) {
  const [filterDir, setFilterDir] = useState<'all' | 'in' | 'out'>('all');
  const [search, setSearch] = useState('');
  const [redactContent, setRedactContent] = useState(true);
  const [selectedFrameId, setSelectedFrameId] = useState<string | null>(null);
  const [copied, setCopied] = useState(false);

  // Redaction helper: Sanitizes payload content strings without modifying protocol headers
  const sanitizePayload = (raw: string, redact: boolean): string => {
    if (!redact) return raw;
    try {
      const parsed = JSON.parse(raw) as Record<string, unknown>;
      if (typeof parsed.content === 'string') {
        const len = parsed.content.length;
        parsed.content = `[PROTECTED - ${len} chars]`;
      }
      return JSON.stringify(parsed, null, 2);
    } catch {
      // For plain text, redact after command
      return raw.replace(/^(MSG|PRIVATE\s+\S+)\s+(.+)$/i, '$1 [PROTECTED]');
    }
  };

  const filteredFrames = useMemo(() => {
    return frames.filter(f => {
      if (filterDir === 'in' && f.direction !== 'in') return false;
      if (filterDir === 'out' && f.direction !== 'out') return false;
      if (search) {
        const q = search.toLowerCase();
        const matchesSummary = f.summary.toLowerCase().includes(q);
        const matchesType = f.type?.toLowerCase().includes(q);
        const matchesCorr = f.correlationId?.toLowerCase().includes(q);
        const matchesRaw = f.rawText.toLowerCase().includes(q);
        if (!matchesSummary && !matchesType && !matchesCorr && !matchesRaw) return false;
      }
      return true;
    });
  }, [frames, filterDir, search]);

  const activeFrame = useMemo(() => {
    if (!selectedFrameId) return filteredFrames[0] ?? null;
    return frames.find(f => f.id === selectedFrameId) ?? filteredFrames[0] ?? null;
  }, [frames, filteredFrames, selectedFrameId]);

  const handleCopy = (text: string) => {
    navigator.clipboard.writeText(text).then(() => {
      setCopied(true);
      setTimeout(() => setCopied(false), 1500);
    });
  };

  return (
    <div className="protocol-lens-view">
      <div className="lens-toolbar">
        <div className="lens-filters">
          <div className="tab-group" role="tablist">
            <button
              className={`tab-btn ${filterDir === 'all' ? 'active' : ''}`}
              onClick={() => setFilterDir('all')}
            >
              All ({frames.length})
            </button>
            <button
              className={`tab-btn ${filterDir === 'in' ? 'active' : ''}`}
              onClick={() => setFilterDir('in')}
            >
              Inbound (←)
            </button>
            <button
              className={`tab-btn ${filterDir === 'out' ? 'active' : ''}`}
              onClick={() => setFilterDir('out')}
            >
              Outbound (→)
            </button>
          </div>

          <div className="search-input-wrap" style={{ maxWidth: 220 }}>
            <IcSearch size={14} className="search-icon" />
            <input
              type="text"
              className="input-field"
              placeholder="Filter type, ID, or text…"
              value={search}
              onChange={e => setSearch(e.target.value)}
              style={{ paddingLeft: 28, height: 32, fontSize: 12 }}
            />
          </div>
        </div>

        <div className="lens-actions">
          <label className="redaction-toggle" title="Protect confidential chat content from screen leakage">
            <input
              type="checkbox"
              checked={redactContent}
              onChange={e => setRedactContent(e.target.checked)}
            />
            <span style={{ display: 'inline-flex', alignItems: 'center', gap: 4, fontSize: 12 }}>
              <IcShield size={14} /> Redact Payloads
            </span>
          </label>

          <button className="btn-secondary" onClick={onClear} title="Clear frame log" style={{ height: 32, fontSize: 12 }}>
            <IcRefresh size={13} /> Clear
          </button>
        </div>
      </div>

      <div className="lens-split">
        {/* Frame List */}
        <div className="lens-frame-list" role="list">
          {filteredFrames.length === 0 ? (
            <div className="lens-empty-state">
              No protocol frames recorded matching filter criteria.
            </div>
          ) : (
            filteredFrames.map(f => {
              const isSelected = activeFrame?.id === f.id;
              const isIn = f.direction === 'in';
              return (
                <div
                  key={f.id}
                  className={`lens-frame-row ${isSelected ? 'selected' : ''}`}
                  onClick={() => setSelectedFrameId(f.id)}
                  role="listitem"
                >
                  <div className="lens-row-top">
                    <span className={`direction-badge ${isIn ? 'in' : 'out'}`}>
                      {isIn ? '← IN' : '→ OUT'}
                    </span>
                    <span className="lens-type mono">{f.type ?? 'RAW'}</span>
                    <span className="lens-bytes">{f.wireBytes} B</span>
                  </div>

                  <div className="lens-summary mono">
                    {f.summary}
                  </div>

                  <div className="lens-meta">
                    <span>{f.timestamp.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' })}</span>
                    {f.correlationId && <span className="mono">id:{f.correlationId}</span>}
                    <span className="proto-pill">{f.protocolVersion}</span>
                  </div>
                </div>
              );
            })
          )}
        </div>

        {/* Frame Detail Inspector */}
        <div className="lens-detail-panel">
          {activeFrame ? (
            <div className="lens-detail-content">
              <div className="detail-header">
                <div>
                  <h4 style={{ margin: 0, fontSize: 14 }}>
                    {activeFrame.direction === 'in' ? 'Inbound Frame (←)' : 'Outbound Frame (→)'}
                  </h4>
                  <span style={{ fontSize: 11, color: 'var(--text-secondary)' }}>
                    {activeFrame.timestamp.toISOString()}
                  </span>
                </div>
                <button
                  className="btn-secondary"
                  onClick={() => handleCopy(sanitizePayload(activeFrame.rawText, redactContent))}
                  style={{ padding: '3px 10px', fontSize: 11 }}
                >
                  {copied ? (
                    <>
                      <IcCheck size={12} /> Copied
                    </>
                  ) : (
                    'Copy Frame'
                  )}
                </button>
              </div>

              {/* Wire size gauge */}
              <div className="wire-gauge-wrap">
                <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 11, marginBottom: 4 }}>
                  <span>Wire Size: <strong>{activeFrame.wireBytes} bytes</strong></span>
                  <span style={{ color: 'var(--text-secondary)' }}>Limit: 2048 bytes (newline framed)</span>
                </div>
                <div className="wire-gauge-bar">
                  <div
                    className="wire-gauge-fill"
                    style={{
                      width: `${Math.min(100, (activeFrame.wireBytes / 2048) * 100)}%`,
                      backgroundColor: activeFrame.wireBytes > 2048 ? 'var(--error)' : 'var(--accent)',
                    }}
                  />
                </div>
              </div>

              {/* Attributes breakdown */}
              <div className="lens-attributes-table">
                <div className="attr-row">
                  <span className="attr-key">Contract Type</span>
                  <span className="attr-val mono">{activeFrame.type ?? '(None)'}</span>
                </div>
                <div className="attr-row">
                  <span className="attr-key">Correlation ID</span>
                  <span className="attr-val mono">{activeFrame.correlationId ?? '(None)'}</span>
                </div>
                <div className="attr-row">
                  <span className="attr-key">Protocol Mode</span>
                  <span className="attr-val">{activeFrame.protocolVersion}</span>
                </div>
                <div className="attr-row">
                  <span className="attr-key">Privacy Mode</span>
                  <span className="attr-val" style={{ color: redactContent ? 'var(--accent)' : 'var(--pending)' }}>
                    {redactContent ? 'Active (Payload contents masked)' : 'Raw Unmasked'}
                  </span>
                </div>
              </div>

              <div style={{ marginTop: 12 }}>
                <span className="fr-label" style={{ display: 'block', marginBottom: 4 }}>
                  Wire Representation {redactContent && '(Redacted)'}:
                </span>
                <pre className="lens-raw-box mono">
                  {sanitizePayload(activeFrame.rawText, redactContent)}
                </pre>
              </div>
            </div>
          ) : (
            <div className="lens-empty-state">
              Select a frame to inspect its protocol details and wire payload.
            </div>
          )}
        </div>
      </div>
    </div>
  );
}

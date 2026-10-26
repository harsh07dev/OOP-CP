/** ComposeBar.tsx — Bottom message input with send button. */
import { useRef, type KeyboardEvent } from 'react';
import { IcSend } from './icons';
import type { ChatContext } from '../types';

interface ComposeBarProps {
  context: ChatContext;
  onSend: (body: string) => void;
  disabled?: boolean;
}

function placeholderFor(ctx: ChatContext): string {
  switch (ctx.type) {
    case 'global':
      return 'Broadcast to everyone…';
    case 'room':
      return `Message #${ctx.room}…`;
    case 'dm':
      return `Message ${ctx.peer}…`;
  }
}

export default function ComposeBar({ context, onSend, disabled }: ComposeBarProps) {
  const textareaRef = useRef<HTMLTextAreaElement>(null);

  const send = () => {
    const ta = textareaRef.current;
    if (!ta) return;
    const body = ta.value.trim();
    if (!body) return;
    onSend(body);
    ta.value = '';
    // Reset height
    ta.style.height = 'auto';
  };

  const handleKey = (e: KeyboardEvent<HTMLTextAreaElement>) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      send();
    }
  };

  const handleInput = () => {
    const ta = textareaRef.current;
    if (!ta) return;
    ta.style.height = 'auto';
    ta.style.height = Math.min(ta.scrollHeight, 160) + 'px';
  };

  return (
    <div className="compose-bar">
      <div className="compose-bar__inner">
        <textarea
          id="compose-input"
          ref={textareaRef}
          className="compose-bar__input"
          placeholder={placeholderFor(context)}
          rows={1}
          disabled={disabled}
          onKeyDown={handleKey}
          onInput={handleInput}
          aria-label="Compose message"
        />
        <button
          id="send-btn"
          className="compose-bar__send"
          onClick={send}
          disabled={disabled}
          aria-label="Send message"
          title="Send (Enter)"
        >
          <IcSend size={15} />
        </button>
      </div>
      <div className="text-xs text-secondary" style={{ marginTop: 5, paddingLeft: 4 }}>
        Press Enter to send · Shift+Enter for new line
      </div>
    </div>
  );
}

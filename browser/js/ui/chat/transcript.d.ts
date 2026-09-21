import type { ChatRow } from './row/chatRowMenuModel.js';

/** Keyed by row id, incremental; both surfaces call this on every log change. */
export declare const renderChatLog: (
  transcript: HTMLElement, log: readonly ChatRow[],
  hooks?: {
    onConfigure?: () => void;
    onAskSubmit?: (answer: string, row: ChatRow) => void;
    onRetry?: (text: string) => void;
    onReconnect?: (serverUrl: string) => void;
  },
) => void;

/** Pins to the bottom now and again as the entrance animations settle. */
export declare const stickToBottom: (transcript: HTMLElement) => void;

export declare const wireChatSuggestions: (transcript: HTMLElement, onPick: (prompt: string) => void) => void;

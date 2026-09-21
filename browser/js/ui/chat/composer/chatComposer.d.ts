export declare const chatComposerActionsHtml: (opts: {
  prefix: string; actionsClass: string; gearClass: string; trailingHtml?: string;
}) => string;

/** The most recently opened "…" trigger that is still on screen, or null. */
export declare const visibleChatMoreBtn: (doc?: Document) => HTMLElement | null;

export declare const wireChatMoreMenu: (
  prefix: string, doc?: Document, opts?: { onOpen?: () => void },
) => void;

/** Both surfaces share the one layoutPrefs.js preference. */
export declare const wireChatSideToggle: (prefix: string, transcript: HTMLElement | null, doc?: Document) => void;

export interface ComposerVoiceUi {
  on: boolean;
  listening: boolean;
}

export declare const syncComposerControls: (
  els: { sendBtn: HTMLButtonElement; attachBtn: HTMLButtonElement; input: HTMLInputElement | HTMLTextAreaElement },
  sending: boolean,
  opts?: { attachFull?: boolean; voice?: ComposerVoiceUi | null; voiceSupported?: boolean },
) => void;

export interface SendGesture {
  click(): void;
  dblclick(): void;
  pressStart(pos?: { x?: number; y?: number }): void;
  pressMove(pos?: { x?: number; y?: number }): void;
  pressEnd(): void;
}

export declare const createSendGesture: (opts?: {
  onClick: () => void; onSwitch: () => void; delay?: number; holdMs?: number; slop?: number;
  setTimer?: (fn: () => void, ms: number) => unknown; clearTimer?: (id: unknown) => void;
}) => SendGesture;

export interface ComposerVoice {
  isOn(): boolean;
  isListening(): boolean;
  toggleMode(): void;
  toggleListening(): void;
}

/** Enter sends, Shift+Enter newlines, send doubles as Stop. Returns `send` for voice wiring. */
export declare const wireChatComposer: (
  els: { input: HTMLInputElement | HTMLTextAreaElement; sendBtn: HTMLButtonElement; attachBtn: HTMLButtonElement; attachInput: HTMLInputElement },
  deps: {
    isSending: () => boolean; abort: () => void; submit: (text: string) => void;
    attachFiles: (files: File[]) => Promise<void>; onInput?: () => void; voice?: ComposerVoice | null;
  },
) => () => void;

// Voice input modes: the one coordinator behind every way of talking to the assistant.
// 'composer' dictates into a chat textarea (only a spoken send phrase sends; a pause ends
// the dictation); 'chat' is the toolbar's hands-free VOICE CHAT (each utterance is a
// logged turn; a pause OR the phrase sends). One mode listens at a time.
import type { DrawingApp } from '../../core/drawingApp.js';
import type { VoiceInput } from './voiceInput.js';
import type { VoiceSettings } from './voiceSettings.js';

export type VoiceMode = 'off' | 'composer' | 'chat';

/** A composer that holds the mic. `onStop` hears why it lost it. */
export interface ComposerTarget {
  setText(text: string): void;
  submit(reason: string): void;
  onStop?(reason: string): void;
  /** Lets a bare "send it" act on words already in the box. */
  hasText?(): boolean;
}

/** The detail published on VOICE_STATE_EVENT. */
export interface VoiceStateDetail {
  mode: VoiceMode;
  listening: boolean;
  supported: boolean;
  reason?: string;
  error?: string;
}

export interface VoiceModesDeps {
  engine?: VoiceInput;
  /** A voice-chat utterance to send as a turn; `reason` is 'silence' | 'phrase' | 'stop'. */
  sendTurn?: (text: string, opts: { reason: string }) => unknown;
  loadSettings?: () => VoiceSettings;
  setTimer?: (fn: () => void, ms: number) => unknown;
  clearTimer?: (id: unknown) => void;
  now?: () => number;
  notify?: ((text: string, type: string) => void) | null;
  /** The event target for VOICE_STATE_EVENT / VOICE_SETTINGS_EVENT. */
  win?: EventTarget | null;
}

export interface VoiceModes {
  readonly supported: boolean;
  readonly mode: VoiceMode;
  readonly listening: boolean;
  readonly level: number;
  readonly target: ComposerTarget | null;
  /** The hands-free toggle; setting it on throws UNSUPPORTED_TEXT where the browser cannot listen. */
  voiceChat: boolean;
  /** True only if the composer really holds the mic afterwards. */
  startComposer(target: ComposerTarget): boolean;
  stopComposer(target?: ComposerTarget): void;
  toggleComposer(target: ComposerTarget): boolean;
  stopAll(reason?: string): void;
  /** Subscribe to the level stream; returns the unsubscribe. */
  onLevel(fn: (level: number) => void): () => void;
  dispose(): void;
}

export declare const VOICE_STATE_EVENT: string;
/** Level above which a frame counts as speech (speech reads 0.4–1.0, room noise < 0.1). */
export declare const VOICE_ACTIVE_LEVEL: number;
export declare const UNSUPPORTED_TEXT: string;
/** The spoken send, longest phrase first, only as the whole tail of an utterance. */
export declare const SEND_PHRASES: string[];
export declare const splitSendPhrase: (raw: unknown) => { text: string; send: boolean; phrase: string | null };
export declare const createVoiceModes: (deps?: VoiceModesDeps) => VoiceModes;
/** The app's one coordinator, on `app.voice`; voice-chat turns share the logged-turn frame. */
export declare const installVoiceModes: (app: DrawingApp, over?: VoiceModesDeps) => VoiceModes;

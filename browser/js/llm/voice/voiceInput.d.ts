// Speech recognition engine: one continuous Web Speech session that survives the browser's
// own end-of-utterance stops (restarted from `onend` while the caller still wants to
// listen), plus the level meter. No send policy — voiceModes.js decides WHEN an utterance
// is done; this says WHAT has been heard since the last commit().
import type { LevelMeter } from './voiceLevel.js';

export type VoiceState = 'idle' | 'starting' | 'listening' | 'stopping';

/** Final text so far, the interim tail, and both joined. */
export interface VoiceTranscript { final: string; interim: string; text: string; }

export interface VoiceError { code: string; fatal: boolean; text: string; }

export interface VoiceSession {
  /** A BCP-47 tag; only a change of it recreates the recognizer on a hot-swap. */
  lang: string;
  onTranscript?: (t: VoiceTranscript) => void;
  onLevel?: (level: number) => void;
  onState?: (state: VoiceState, extra?: { error?: string }) => void;
  onError?: (err: VoiceError) => void;
}

/** The slice of the Web Speech `SpeechRecognition` instance this engine drives. */
export interface SpeechRecognitionLike {
  continuous: boolean;
  interimResults: boolean;
  maxAlternatives: number;
  lang: string;
  onstart: (() => void) | null;
  onresult: ((e: { results: ArrayLike<ArrayLike<{ transcript: string }> & { isFinal: boolean }> }) => void) | null;
  onerror: ((e: { error?: string }) => void) | null;
  onend: (() => void) | null;
  start(): void;
  stop(): void;
  abort(): void;
}

export interface VoiceInputDeps {
  SpeechRecognition?: new () => SpeechRecognitionLike;
  createLevelMeter?: () => LevelMeter;
  setTimer?: (fn: () => void, ms: number) => unknown;
  clearTimer?: (id: unknown) => void;
}

export interface VoiceInput {
  readonly supported: boolean;
  /** The caller's intent: keep listening. */
  readonly active: boolean;
  readonly state: VoiceState;
  readonly listening: boolean;
  readonly transcript: VoiceTranscript;
  /** Begin, or while active HOT-SWAP, a session. False when the browser cannot listen. */
  start(next: VoiceSession): boolean;
  /** Everything reported so far is consumed — a phrase can never be sent twice. */
  commit(): void;
  /** Graceful and idempotent: pending finals still arrive, then the state settles to 'idle'. */
  stop(): void;
}

export declare const isVoiceSupported: (win?: object | null) => boolean;
/** Errors after which listening cannot continue; the rest restart. */
export declare const FATAL_ERRORS: Set<string>;
/** Restart delays after consecutive network drops; one more than its length is fatal. */
export declare const NETWORK_BACKOFF_MS: number[];
export declare const voiceErrorText: (code: string | null | undefined) => string;
export declare const createVoiceInput: (deps?: VoiceInputDeps) => VoiceInput;

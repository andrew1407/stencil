import type { VoiceModes } from '../../llm/voice/voiceModes.js';

export interface ComposerVoiceApi {
  isOn(): boolean;
  isListening(): boolean;
  toggleMode(): void;
  toggleListening(): void;
  state(): { on: true; listening: boolean } | null;
  supported(): boolean;
  setMode(next: boolean): void;
  target: unknown;
}

/** Wires the mic face of one composer's Send button to the shared voice coordinator. */
export declare const wireComposerVoice: (opts: {
  prefix: string; input: HTMLInputElement | HTMLTextAreaElement; sendBtn: HTMLButtonElement;
  doc?: Document; app: { voice?: VoiceModes | null }; send: () => void; sync: () => void; win?: Window | null;
}) => ComposerVoiceApi;

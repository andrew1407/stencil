// Shapes for popup/assistant/boot.js — lazy boot: wires the composer's controls, drop/
// paste-to-attach and the provider status probe the first time the section opens.
export declare function createBoot(opts: {
  sectionEl: HTMLElement;
  transcriptEl: HTMLElement;
  inputEl: HTMLTextAreaElement;
  sendBtn: HTMLButtonElement;
  clearBtn: HTMLButtonElement;
  view: unknown;
  tray: unknown;
  send: (preset?: string) => Promise<void>;
  state: { booted: boolean; llmSettings: unknown; probeGen: number; settingsPromise: Promise<unknown> | null };
}): () => void;

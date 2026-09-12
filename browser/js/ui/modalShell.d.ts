import type { ModalShellApi } from './modalRegistry.js';

/** What createModalFlight (ui/modalFlight.js) hands a shell. */
export interface ModalFlight {
  reducedMotion(): boolean;
  setOrigin(anchor: unknown): boolean;
  finishClose(): void;
  playDust(enter: boolean): void;
  playClosing(): void;
  settle(): void;
}

export interface ModalShellOptions {
  onOpen?: () => void;
  onClose?: () => void;
  escapeClose?: boolean;
  originEl?: (() => unknown) | null;
  stacked?: boolean;
}

/** Wire open/close/overlay-press/Escape for one app modal. */
export declare function wireModalShell(
  overlay: unknown, openBtn: unknown, closeBtn: unknown, opts?: ModalShellOptions,
): ModalShellApi;

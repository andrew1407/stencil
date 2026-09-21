/** The live handle ui/modalShell.js registers for one wired window. */
export interface ModalShellApi {
  open(from?: unknown, backTo?: unknown, opts?: { stacked?: boolean }): void;
  close(): void;
  openPopover(anchorEl: unknown): void;
  toggle(from?: unknown): void;
  isOpen(): boolean;
  readonly stacked: boolean;
  takesEscape(): boolean;
}

/** Every wired shell, in wire order. */
export declare const modalShells: Set<ModalShellApi>;

/** Installs the single document-level Escape listener; safe to call per shell. */
export declare function wireEscapeOnce(): void;

/** Close every open window except `except`; returns the first one closed, else null. */
export declare function closeOpenModal(except?: ModalShellApi | null): ModalShellApi | null;

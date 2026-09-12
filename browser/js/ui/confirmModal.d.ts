import type { StencilElement } from './base.js';

export interface ConfirmOptions {
  title?: string; titleIcon?: string; confirmLabel?: string; cancelLabel?: string;
  confirmIcon?: string; danger?: boolean; closeAnchor?: Element | null;
}
export interface ConfirmAltOptions extends ConfirmOptions { altLabel?: string; altIcon?: string; }
export interface ChooseOptions extends ConfirmOptions { options?: { value: string; label?: string }[]; }
export interface PromptOptions extends ConfirmOptions {
  defaultValue?: string; multiline?: boolean; rows?: number;
  validate?: (trimmed: string) => string;
}

/** The reusable yes/no modal behind app.confirm(); resolves false on any dismissal. */
export declare class StencilConfirmModal extends StencilElement {
  static inner(): string;
  static template(): string;
  wire(): void;
  ask(message: string, opts?: ConfirmOptions): Promise<boolean>;
  /** Cancel | altLabel | confirmLabel, resolving 'confirm', 'alt' or null. */
  askAlt(message: string, opts?: ConfirmAltOptions): Promise<'confirm' | 'alt' | null>;
  choose(message: string, opts?: ChooseOptions): Promise<string | null>;
  prompt(message: string, opts?: PromptOptions): Promise<string | null>;
}

import type { StencilElement } from '../base.js';

/** Desktop parity: Notifications::MAX_VISIBLE. */
export declare const MAX_VISIBLE: number;
/** A whitespace-free run longer than `max` gets a middle ellipsis. */
export declare const squeezeLongTokens: (msg: unknown, max?: number) => string;

/** The bottom-left notification stack; utils.js notify() delegates here. */
export declare class StencilNotifications extends StencilElement {
  static inner(): string;
  static template(): string;
  /** `onClick` makes the toast clickable (longer linger); `key` replaces a running status;
   *  `shine` is a logo show's own notice: the egg on gold, with the golden shining around it. */
  notify(msg: string, type?: 'ok' | 'fail' | 'info',
    opts?: { onClick?: (() => void) | null; key?: string | null; shine?: boolean }): void;
}

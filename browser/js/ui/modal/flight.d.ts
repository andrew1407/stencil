import type { ModalFlight } from './shell.js';
/** How long the close flight lasts — the dust's own clock (config/motion.json). */
export declare const MODAL_CLOSE_MS: number;
/** The element's client rect while it is really on screen (not folded, hidden, faded or clipped), else null. */
export declare const shownRect: (el: unknown) => { left: number; top: number; right: number; bottom: number; width: number; height: number } | null;
/** The grow-from-the-icon / pour-back flight for one overlay and its box. */
export declare function createModalFlight(overlay: unknown, boxOf: () => unknown): ModalFlight;

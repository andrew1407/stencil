import type { ModalFlight } from './shell.js';
/** How long the close flight lasts — the dust's own clock (config/motion.json). */
export declare const MODAL_CLOSE_MS: number;
/** The grow-from-the-icon / pour-back flight for one overlay and its box. */
export declare function createModalFlight(overlay: unknown, boxOf: () => unknown): ModalFlight;

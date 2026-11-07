import type { Point } from '../../core/geometry.js';
export declare const DUST_CURSOR_PX: number;
/** ms a tip stays up while the pointer crosses the gap to the next control. */
export declare const LEAVE_GRACE_MS: number;
export declare const dustOrigin: (centre: Point | null, cursor: Point | null, maxPx?: number) => Point | null;

export { parseCombo, eventCombo, comboMatchesEvent } from '../control/comboMatch.js';

export declare const dismissTip: () => void;
export declare const initTooltips: () => void;

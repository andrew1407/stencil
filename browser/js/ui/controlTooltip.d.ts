import type { Point } from '../core/geometry.js';
export declare const DUST_CURSOR_PX: number;
export declare const dustOrigin: (centre: Point | null, cursor: Point | null, maxPx?: number) => Point | null;

export { parseCombo, eventCombo, comboMatchesEvent } from './comboMatch.js';

export declare const dismissTip: () => void;
export declare const initTooltips: () => void;

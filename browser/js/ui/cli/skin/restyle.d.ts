// A run in the accent re-dressed in the skin that is on (restyle.zig).
import type { SkinFrame } from './pictures.js';
export declare const PICTURE_CLASS: string;
export declare const HUE_PERIOD: number;
export declare const restyles: (skin: string | null) => boolean;
export declare const accentUnder: (skin: string | null, accent: string) => string;
export declare const hueGradient: (skin: string) => string;
export declare const rowKey: (text: string) => number;
export declare const swappable: (ch: string) => boolean;
export declare const fillRun: (el: HTMLElement, text: string, opts?: { skin?: string | null; clock?: SkinFrame | null; key?: number; col0?: number }) => void;

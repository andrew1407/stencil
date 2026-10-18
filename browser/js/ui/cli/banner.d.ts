// The pinned logo of the CLI stage, painted cell by cell from the core's banner rows.
import type { SkinFrame } from './skin/pictures.js';
export interface BannerCell { ch: string; role: 'accent' | 'mark' | 'panel' | 'letter' | 'picture'; c: number; }
export declare const WORDMARK: string;
export declare const markOf: (skin: string | null) => 'S' | 'P' | 'M';
export declare const wordmarkOf: (skin: string | null) => string;
export declare const bannerCells: (skin: string | null, compact: boolean, clock?: SkinFrame | null) => BannerCell[][];
export declare const wordmarkCells: (skin: string | null, clock?: SkinFrame | null) => BannerCell[];
export declare const paintBanner: (pre: HTMLElement, opts?: { skin?: string | null; compact?: boolean; accent: string; clock?: SkinFrame | null; frame?: number }) => void;

// The secret skins and their frame clock.
export interface SkinTraits {
  word: string; phrase?: string; phraseOff?: string; wordmark?: string; wordmarkAccented?: boolean;
  animated?: boolean; movesLogo?: boolean; paintsRuns?: boolean; slides?: boolean; paintsCells?: boolean;
  replacesLetters?: boolean; fg?: string; bg?: string; titleFg?: string; titleBg?: string; wash?: string;
  mark?: 'P' | 'M'; markColor?: string; title?: string; body?: string;
}
export interface SkinClock {
  start: (nowMs: number, seed?: number) => void; syncClock: (nowMs: number) => boolean; tick: () => void;
  frame: () => number; seed: () => number; nextBulb: () => string | null;
}
export declare const SKIN_NAMES: readonly string[];
export declare const LIST_WORD: string;
export declare const GOLD: string;
export declare const FRAME_MS: number;
export declare const traitsOf: (name: string) => SkinTraits | null;
export declare const skinOf: (word: unknown) => string | null;
export declare const isSecret: (word: unknown) => boolean;
export declare const createSkinClock: () => SkinClock;
export declare const hueAt: (name: string, col: number, frame: number) => string;

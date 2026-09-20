// The logo stage's table and the pure rules over it (config/logoStage.json).
export type StageEffect = 'neon' | 'sun' | 'fire' | 'water' | 'dust' | 'shrink' | 'grow'
  | 'follow' | 'escape' | 'pink' | 'fly';
export type StageStyle = 'dust' | 'water' | 'fire';

export interface StageShow {
  effect: StageEffect;
  accents?: string[];
  motion?: string;
  customHex?: string;
}
export interface StageConfig {
  holdMs: number;
  toast: string;
  toastGold: string;
  toastInk: string;
  toastGlow: string;
  shows: Record<string, StageShow>;
  stage: { logoShare: number; minShare: number; revealMs: number; hideMs: number; beatMs: number;
    spinMs: number; holdBoost: number; holdRampMs: number; scrimAlpha: number; roamShare: number;
    bounceBigShare: number; markEdgeShare: number; markCornerShare: number };
  glow: { alphaMin: number; alphaMax: number; reachShare: number; steadyLit: number; floor: number };
  sun: { spokes: number; gapShare: number; lengthShare: number; alphaMin: number; alphaMax: number;
    softWidthShare: number; brightWidthShare: number };
  cloud: { rate: number; lifeMs: [number, number]; marginShare: number; maxLive: number;
    speedShare: [number, number]; sizeShare: [number, number]; tailSpreadTurns: number; tailGapShare: number;
    tailSpeedScale: number; tailMinSpeedPx: number };
  bounce: { snapMs: number; recoverMs: number };
  follow: { stiffness: number; dragPerS: number };
  escape: { radiusPx: number; stiffness: number; dragPerS: number };
  fly: { speedPx: number; punchPx: number; dampPerS: number };
  pink: { blank: string; tint: string; heartStroke: string; heartFill: string; heartThickness: number;
    heartPoints: number; heartInsetShare: number };
}

export declare const STAGE: StageConfig;
export declare const SHOWS: Readonly<Record<string, StageShow>>;
export declare const SHOW_NAMES: readonly string[];
/** The show names lower-cased: what the typed trigger listens for. */
export declare const TYPED_WORDS: readonly string[];
export declare const HOLD_MS: number;
export declare const TOAST_TEXT: string;

export declare const effectOf: (name: string) => StageEffect | null;
/** The show a hold opens for this accent, custom hex and motion mode; null = nothing. */
export declare const resolveShow: (accentKey: string, customHex: string | null, motionMode: string) => string | null;
/** The cloud a show wears: its own for a styled show, the current one for a roaming show. */
export declare const showStyle: (name: string, currentStyle?: StageStyle | null) => StageStyle | null;
/** Where a ray at `angle` from the centre meets the mark's own rounded-square outline. */
export declare const markEdge: (size: number, angle: number) => StagePoint;
export declare const bigLogoSize: (w: number, h: number) => number;
/** The big end of a BOUNCING show (shrink, grow), which fills far more of the window. */
export declare const bounceBigSize: (w: number, h: number) => number;
/** True for the shows whose mark snaps between two sizes (shrink, grow). */
export declare const bounces: (name: string) => boolean;
/** True for the shows whose mark travels the window (follow, escape, fly). */
export declare const roams: (name: string) => boolean;
/** The small mark a roaming show wears. */
export declare const roamLogoSize: (w: number, h: number) => number;
export declare const minLogoSize: (w: number, h: number) => number;

export interface StagePoint { x: number; y: number; }
/** The heart fitted to the centred square of a w×h image, y down, n points round. */
export declare const heartPoints: (w: number, h: number, n?: number) => StagePoint[];
export interface HeartLine {
  points: StagePoint[];
  locked: true;
  color: string;
  fillColor: string;
  thickness: number;
}
export declare const heartLine: (w: number, h: number) => HeartLine;

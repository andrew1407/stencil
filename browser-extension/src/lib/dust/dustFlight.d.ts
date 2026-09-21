// A dust grain's flight: the FLIGHTS table, the eased legs behind it and the palette/tint ramp.
// Byte-pinned twin: browser-extension/src/lib/dust/dustFlight.js (browser-extension/tests/portParity.test.js).
export interface Flight {
  from: 'home' | 'far'; split: number; leg: (t: number) => number; rest: (t: number) => number;
  alpha: [number, number][];
}
export declare const FLIGHTS: Record<'scatter' | 'gather' | 'surfaceGather' | 'surfaceScatter' | 'fall', Flight>;
export declare const bezierY: (t: number, x1: number, y1: number, x2: number, y2: number) => number;
export declare const EASE_STEPS: number;
export declare const easeLut: (x1: number, y1: number, x2: number, y2: number) => (t: number) => number;
export declare const alphaAt: (stops: [number, number][], p: number) => number;
export declare const TURBULENCE_SHARE: number;
export declare const TURBULENCE_MAX_PX: number;
export declare const TURBULENCE_WAVES: [number, number];
export declare const TWINKLE_DEPTH: number;
export declare const TWINKLE_HZ: [number, number];
export declare const turbulenceAt: (m: Mote, p: number) => number;
export declare const twinkleAt: (m: Mote, tMs: number) => number;
export declare const STYLE_DUST: 0;
export declare const STYLE_WATER: 1;
export declare const STYLE_FIRE: 2;
export declare const PARTICLE_STYLES: Record<'dust' | 'water' | 'fire', ParticleStyle>;
export declare const PALETTE_STOPS: number;
export declare const WATER: Record<string, number | [number, number]>;
export declare const FIRE: Record<string, number | [number, number]>;
export declare const styleFrame: (style: ParticleStyle, p: number, away: number, w: number, len: number,
export declare const DUST_MIX_SPREAD: number;
export declare const dustMix: (w: number, glint: boolean) => number;
export declare const paletteIndex: (mix: number, stops?: number) => number;
export declare const TINT_SHARE: number;
export declare const TINT_CSS: string[];
export declare const TINT_STOPS: number;
export declare const PAINT_STOPS: number;
export declare const tintOf: (w: number) => number;
export declare const stopOfTint: (mix: number, tint: number) => number;
export declare const hashNoise: (a: number, b: number) => number;

// Shared inside the dust family only: the tuning table this module reads, the eased-leg builder
// the FLIGHTS table is made of, and the fractional part a grain's hash is folded with.
export declare const TUNE: Record<string, unknown>;
export declare const leg: (x1: number, y1: number, x2: number, y2: number) => (t: number) => number;
export declare const fract: (v: number) => number;

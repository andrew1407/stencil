// Pure geometry/particle math is exported for node tests; the DOM-driving half is guarded.
export declare const DUST_MARGIN: number;
export declare const DUST_SILENCE: number;
export declare const DUST_LIFE_MS: [number, number];
export declare const DUST_RATE: number;
export declare const DUST_TINTS: number;
export declare const RING_SPOKES: number;
export declare const RING_SPIN_MS: number;
export declare const RING_BEAT_MS: number;

/** How far the ring's spokes reach from a w×h tile's centre, given the voice level. */
export declare const ringRadius: (w: number, h: number, level: number) => number;
/** The ring's opacity at time t (a breathing beat). */
export declare const ringAlpha: (t: number) => number;
/** Spoke angles at time t: evenly spaced, the set turning with RING_SPIN_MS. */
export declare const ringAngles: (t: number) => number[];

export type RGB = [number, number, number];
/** 'rgb(...)' / 'rgba(...)' / '#rrggbb' → [r, g, b], or null. */
export declare const parseRgb: (str: unknown) => RGB | null;
export declare const mixRgb: (a: RGB, b: RGB, t: number) => RGB;
/** Evenly spaced 'rgb(...)' tints from `ink` to `accent`. */
export declare const dustPalette: (ink: unknown, accent: unknown, stops?: number) => string[];

/** Motes to spawn this frame, given the level, elapsed ms and an injectable RNG. */
export declare const spawnCount: (level: number, dt?: number, rnd?: () => number) => number;
/** Where a ray at `angle` from a w×h tile's centre meets its edge. */
export declare const edgePoint: (w: number, h: number, angle: number) => { x: number; y: number };

export interface VoiceMote {
  x: number; y: number; vx: number; vy: number; age: number; life: number; size: number; tint: number;
}
/** A mote born on the edge of a w×h tile centred at (0,0). */
export declare const newMote: (w: number, h: number, level: number, rnd?: () => number) => VoiceMote;
/** Advance a mote by dt ms; false once its life is spent. */
export declare const stepMote: (m: VoiceMote, dt: number) => boolean;
/** Opacity over a mote's life: quick in, long ease-out. */
export declare const moteAlpha: (m: VoiceMote) => number;

/** A z-index one above the highest on `el`'s ancestor chain, never below 5. */
export declare const layerAbove: (el: Element, get?: typeof getComputedStyle | null) => number;

export interface VoiceDustHandle {
  start(): void;
  stop(): void;
  readonly live: number;
}
/** Attach the dust + ray-ring emitter to a mic tile; `isOn` reports whether it is listening. */
export declare const attachVoiceDust: (el: Element | null, isOn: () => boolean) => VoiceDustHandle | null;

// Mirrored byte-for-byte with browser/js/ui/dust/dustCloud.js (portParity.test.js): one
// canvas's worth of animated dust, shared by every element-sized flight in the app.
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

export interface Mote {
  x: number; y: number; dx: number; dy: number; mx: number; my: number; r: number; s: number;
  a: number; w: number; t: number; g: boolean; delay?: number; dur?: number;
}
export declare const turbulenceAt: (m: Mote, p: number) => number;
export declare const twinkleAt: (m: Mote, tMs: number) => number;

export declare const STYLE_DUST: 0;
export declare const STYLE_WATER: 1;
export declare const STYLE_FIRE: 2;
export type ParticleStyle = 0 | 1 | 2;
export declare const PARTICLE_STYLES: Record<'dust' | 'water' | 'fire', ParticleStyle>;
export declare const PALETTE_STOPS: number;
export declare const WATER: Record<string, number | [number, number]>;
export declare const FIRE: Record<string, number | [number, number]>;

export interface StyleFrameOut { sx: number; sy: number; scale: number; glow: number; mix: number; }
export declare const styleFrame: (style: ParticleStyle, p: number, away: number, w: number, len: number,
  tMs: number, out?: StyleFrameOut) => StyleFrameOut;

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

export declare const SHAPE_DISC: 0;
export declare const SHAPE_OVAL: 1;
export declare const SHAPE_WAVE: 2;
export declare const SHAPE_TRIANGLE: 3;
export declare const SHAPE_STREAK: 4;
export type GrainShape = 0 | 1 | 2 | 3 | 4;
export declare const STYLED_CELL_SCALE: number;
export declare const WATER_WAVE_SHARE: number;
export declare const FIRE_STREAK_SHARE: number;
export declare const SHAPES: Record<string, Record<string, number>>;
export declare const grainShape: (style: ParticleStyle, w: number) => GrainShape;
export declare const headingOf: (dx: number, dy: number, fromFar: boolean) => number;
export declare const shapePolygon: (shape: GrainShape, x: number, y: number, r: number, a: number,
  out?: number[]) => number[];
export declare const addGrainPath: (ctx: CanvasRenderingContext2D, shape: GrainShape, x: number, y: number,
  r: number, a: number, scratch?: number[]) => void;

export declare const EDGE_POINTS: number;
export declare const EDGE: Record<'water' | 'fire', Record<string, number>>;
export declare const edgeJitter: (style: ParticleStyle, k: number, points?: number) => number;
export declare const edgeDipOf: (style: ParticleStyle) => number;
export declare const edgeReachOf: (style: ParticleStyle) => number;
export declare const edgeBaseOf: (style: ParticleStyle) => number;
export declare const paletteCss: (stops?: number) => string[];

export interface DrawScratch {
  out: Record<string, number>; buckets: Map<number, number[]>;
  shapes?: Int8Array; heads?: Float32Array; tints?: Int8Array; style?: ParticleStyle; poly?: number[];
}
export declare const moteFrame: (m: Mote, flight: string, p: number, out?: Record<string, number>,
  tMs?: number, style?: ParticleStyle) => Record<string, number>;

export interface CloudBounds { left: number; top: number; right: number; bottom: number; }
export declare const cloudBounds: (motes: Mote[], pad?: number) => CloudBounds | null;

export declare const ALPHA_LEVELS: number;
export declare const FILL_CHUNK: number;
export declare const fillGrains: (ctx: CanvasRenderingContext2D, b: number[], n: number, poly?: number[]) => void;
export declare const drawCloud: (ctx: CanvasRenderingContext2D, motes: Mote[], flight: string, tMs: number,
  colours: string[], scratch?: DrawScratch, style?: ParticleStyle) => void;

export declare const resolveColour: (doc: Document, css: string, probe?: Element | null) => string;

export interface StartCloudOptions {
  flight?: string; span: number; colours: string[]; origin: { x: number; y: number };
  doc?: Document; viewport?: { width: number; height: number } | null;
  raf?: (cb: FrameRequestCallback) => number; now?: () => number; style?: ParticleStyle;
}
export declare function startCloud(host: HTMLElement, motes: Mote[], opts: StartCloudOptions): () => void;

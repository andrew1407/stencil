// One grain of the theme-swap wake — a hand-copied twin of cloud.js's grain maths,
// assigned onto window.StencilKit by the classic script grains.js. No ES exports.
export interface GrainSpec { cx: number; cy: number; size: number; dx: number; dy: number; alpha: number; }
export interface GrainOut { x: number; y: number; r: number; alpha: number; }
export interface StencilKitDustGrains {
  DUST_ALPHA_LEVELS: number;
  dustMix(w: number, glint: boolean): number;
  fillGrains(ctx: CanvasRenderingContext2D, b: number[], n: number, poly: number[]): void;
  grainAt(s: GrainSpec, p: number, out: GrainOut): void;
  grainShape(style: number, w: number): number;
  headingOf(dx: number, dy: number, fromFar: boolean): number;
  paletteCss(): string[];
  resolveColour(css: string): string;
  shapePolygon(shape: number, x: number, y: number, r: number, a: number, out: number[]): number[];
  stopOfTint(mix: number, tint: number): number;
  styleFrame(style: number, p: number, away: number, w: number, len: number, tMs: number,
    out: { sx: number; sy: number; scale: number; glow: number; mix: number }): void;
  tintOf(w: number): number;
}

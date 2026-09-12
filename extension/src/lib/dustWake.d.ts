// The theme-swap wake: grain counts, spawn points, and the canvas stage that paints
// them. Classic script — see accent.js. No ES exports.
export interface DustSpec {
  cx: number; cy: number; size: number; dx: number; dy: number; delay: number; alpha: number;
  accent: boolean; w: number; len: number;
}
export interface DustPaint { palette: string[]; }
export interface StencilKitDustWake {
  dustPaint(): DustPaint | null;
  spawnDust(px: { x: number; y: number; w: number; h: number }, paint: DustPaint | null): void;
}

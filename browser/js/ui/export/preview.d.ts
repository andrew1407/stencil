import type { ExportVariant } from './variants.js';

export interface DustPoint { x: number; y: number; }

/** Called when the owning menu closes, so a stale hover cannot re-show a removed row. */
export declare function clearAltPreviewHover(): void;

/** `point` is the row's centre (the dust's origin/destination); null declines the dust. */
export declare function showExportPreview(
  app: object, variant: ExportVariant, x: number, y: number, point?: DustPoint | null, dustFrom?: DustPoint | null,
): void;

/** `dustTo` overrides where the leave pours into (Alt released = the cursor). */
export declare function hideExportPreview(dustTo?: DustPoint | null): void;

/** Holding Alt over `item` shows the preview for `variant`; releasing Alt or leaving hides it. */
export declare function wireAltPreview(item: HTMLElement, app: object, variant: ExportVariant): void;

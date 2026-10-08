import type { DrawingApp } from '../../../core/drawingApp.js';
import type { HoldZoom } from '../viewport/holdZoom.js';

/** The toolbar element, searched through its scoped `$`. */
export interface ToolbarRoot { $(id: string): HTMLElement | null; }

/** The icons a drop on the canvas fires as their click does. */
export declare const CANVAS_CLICK_IDS: readonly string[];

/** [icon id, what its drop on the canvas does], one row per canvas-drop icon. */
export declare function canvasActs(app: DrawingApp, bar: ToolbarRoot): [string, () => unknown][];

/** Wires every draggable toolbar icon under `bar`; the ids that took a drag. */
export declare function wireToolbarDrags(
  app: DrawingApp,
  bar: ToolbarRoot | null,
  holds?: { in?: HoldZoom; out?: HoldZoom },
): string[];

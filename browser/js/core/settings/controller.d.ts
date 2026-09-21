// The shared editor setters: toolbar handlers AND the console API reach these through
// DrawingApp's delegators, so top-menu settings have one source of truth. Holds no state
// (back-references the app); `persist:false` is for live-drag events that commit later.
import type { DrawingApp } from '../drawingApp.js';

export { COMPARE_MODES } from './registry.js';

export interface SetOptions { persist?: boolean; }
export type TooltipOptionKey = 'enabled' | 'page' | 'screen' | 'coords';
export type VisualColorKey = 'fill' | 'selGlow' | 'hoverRing' | 'focusRing';
export interface MotionPrefs { mode: string; drawing: boolean; }

export declare class SettingsController {
  constructor(app: DrawingApp);
  app: DrawingApp;
  /** Registry-driven setter (registry.js SETTINGS); throws on an unknown key. */
  set(key: string, value: unknown, opts?: SetOptions): void;
  /** Live-apply a setting's VISUAL effect without committing it (dropdown hover preview). */
  preview(key: string, value: unknown): void;
  setColor(v: string, opts?: SetOptions): void;
  setPointColor(v: string, opts?: SetOptions): void;
  setThickness(n: number | string, opts?: SetOptions): void;
  setPointSize(n: number | string, opts?: SetOptions): void;
  setLineStyle(s: string): void;
  setShowPoints(b: boolean): void;
  setShowLines(b: boolean): void;
  setImageFilter(f: string): void;
  setCompareMode(m: string): void;
  /** Divider position for the split compare modes (0..1); transient, redraw only. */
  setCompareSplit(v: number | string): void;
  setFilterColor(v: string, opts?: SetOptions): void;
  setPageSize(size: string): void;
  /** Custom page sides are stored in cm (the model unit). */
  setCustomPageWidth(cm: number): void;
  setCustomPageHeight(cm: number): void;
  setUnit(u: string): void;
  syncFormulaUI(checked: boolean): void;
  showFormulaError(hasError: boolean): void;
  refreshFormulaCoords(): void;
  setAllowFormulas(b: boolean): void;
  /** Wires one f(x,y) input pair (element ids); returns the commit fn for programmatic flushes. */
  wireFormulaInputs(ids: { x: string; y: string; mirrorX: string; mirrorY: string }): () => void;
  /** Throws on an invalid expression so the console surfaces it. */
  setFormula(axis: 'x' | 'y', expr: string | null | undefined): void;
  setTooltipOption(key: TooltipOptionKey, on: boolean): void;
  /** App-wide motion preference (ui/prefs.js); throws on an unknown key or mode. */
  setMotion(key: 'mode' | 'drawing', value: unknown): MotionPrefs;
  setVisualColor(key: VisualColorKey, value: string): void;
}

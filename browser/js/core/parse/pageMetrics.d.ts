// Pixels → page centimetres. A wasm-parity surface (core/page/pageMetrics): the core owns
// the named-size table and the pixel→cm scaling when loaded; the JS is the reference and
// the fallback. Reached as app.getPageDimensions / pixelToPageCoords.
import type { DrawingApp } from '../drawingApp.js';
import type { FormulaContext } from './formulaContext.js';

/** Page size in cm, swapped to landscape when the canvas is wider than tall. */
export declare const getPageDimensions: (app: DrawingApp) => { width: number; height: number };
/** The page (cm), image (px) and display unit a formula's names resolve against. */
export declare const formulaContext: (app: DrawingApp,
  ps?: { width: number; height: number }) => FormulaContext;
/** Raw scaling, then the app's x/y formulas when formulas are allowed. */
export declare const pixelToPageCoords: (app: DrawingApp, x: number, y: number) => { x: number; y: number };

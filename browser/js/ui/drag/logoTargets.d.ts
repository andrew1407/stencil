// What the header logo lands on: a line takes the toolbar style, a control its default, the bare
// canvas the clean view.
import type { DrawingApp } from '../../core/drawingApp.js';

type Control = HTMLSelectElement | HTMLInputElement;

export type LogoTarget =
  | { kind: 'line'; idx: number; el: Element }
  | { kind: 'control'; el: Control }
  | { kind: 'clean'; el: Element };

/** A fullscreen-panel clone's original (same id under #controls-body); anything else is itself. */
export declare const originalOf: <T extends Element | null>(el: T) => T;
/** Resets `el`'s original to its default and keeps the clone showing it; false when nothing moved. */
export declare const resetDropped: (el: Control) => boolean;
/** The enabled control a drop on `target` resets, or null. */
export declare const dropControlAt: (target: EventTarget | null) => Control | null;
/** The element a control's target glow sits on: its custom select, toggle pill or label, else itself. */
export declare const glowOf: (el: Element) => Element;
/** What a pointer at (x, y) over `target` aims at; `viewport` is the canvas region. */
export declare const logoTargetAt: (app: DrawingApp, p: { target: Element | null; x: number; y: number },
  viewport: Element | null, controlAt?: (target: Element) => Control | null) => LogoTarget | null;
/** Whether two aims are the same target. */
export declare const sameTarget: (a: LogoTarget | null, b: LogoTarget | null) => boolean;

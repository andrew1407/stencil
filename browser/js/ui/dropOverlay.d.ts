import type { StencilElement } from './base.js';

/** The full-page image drop overlay: upload & save, or upload incognito. */
export declare class StencilDropOverlay extends StencilElement {
  static inner(): string;
  static template(): string;
}

/** Shown instantly; idempotent (dragover fires it every frame). */
export declare const showDropOverlay: (el: HTMLElement | null) => void;
/** Hidden after a short close animation (pointer-events:none meanwhile); idempotent. */
export declare const hideDropOverlay: (el: HTMLElement | null) => void;

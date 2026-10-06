import type { StencilElement } from '../base.js';

/** The full-page drop overlay: upload & save or incognito, or one zone for a layout / script. */
export declare class StencilDropOverlay extends StencilElement {
  static inner(): string;
  static template(): string;
}

/** Shown instantly; idempotent (dragover fires it every frame). */
export declare const showDropOverlay: (el: HTMLElement | null) => void;
/** Hidden after a short close animation (pointer-events:none meanwhile); idempotent. */
export declare const hideDropOverlay: (el: HTMLElement | null) => void;
/** Split for 'open'; one zone for 'layout' (worded for a missing image) or 'script'. */
export declare const setDropKind: (el: HTMLElement | null, kind: 'open' | 'layout' | 'script', opts?: { hasImage?: boolean }) => void;

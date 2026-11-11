// A skin's life over the page: the stamp, the look its cheat code stores, the watches and the scene.
import type { DrawingApp } from '../../core/drawingApp.js';
import type { SkinLookSettings } from './adopt.js';
import type { SkinScene } from './scene.js';

/** The attribute a skin stamps on `<html>`. */
export declare const SKIN_ATTR: string;
/** The accent hex a skin paints in and the face it is drawn for. */
export interface SkinLook { hex: string | null; dark: boolean }
/** What a skin supplies to its session: its painters, its look, its favicon and its scene. */
export interface SkinSpec {
  name: string;
  /** The motion and accent its cheat code stores as the user's own. */
  look: SkinLookSettings;
  /** The notice the word shows when it takes the skin off. */
  offToast: string;
  /** The favicon art in `hex` while the skin is on. */
  favicon: (hex: string) => string;
  scene: SkinScene;
  /** Everything the skin paints. */
  wear: (app: DrawingApp, doc: Document, look: SkinLook) => void;
  /** The accent moved: the parts that follow it. */
  repaint: (doc: Document, look: SkinLook) => void;
  /** The face moved; `repaint` when absent. */
  onTheme?: (doc: Document, look: SkinLook) => void;
  /** Everything put back as it was. */
  shed: (app: DrawingApp, doc: Document) => void;
}
export interface SkinSession {
  readonly name: string;
  readonly offToast: string;
  /** True while this skin is stamped (or lifted under the CLI stage). */
  active: (doc?: Document) => boolean;
  /** Flip the skin; resolves to the state it is left in. `scene: false` never opens the skin's page. */
  toggle: (app: DrawingApp, doc?: Document, opts?: { scene?: boolean; entered?: boolean }) => Promise<boolean>;
}
export declare const createSkinSession: (skin: SkinSpec) => SkinSession;
/** The skin on the page ('webcore' | 'aqua' | …), a held one included, or null. */
export declare const activeSkin: (doc?: Document) => string | null;
/** Lifts the stamp while the CLI stage is up and remembers it; the name lifted, or null. */
export declare const holdSkin: (doc: Document | null | undefined) => string | null;
/** Puts the held stamp back, unless the skin went off meanwhile. */
export declare const releaseSkin: (doc: Document | null | undefined) => void;

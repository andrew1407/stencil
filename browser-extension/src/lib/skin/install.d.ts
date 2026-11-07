// The session skins' live half on an extension page (browser twin: js/ui/skin/session.js).
/** The accent hex a skin paints in and the face it is drawn for. */
export interface SkinLook { accent: string | null; dark: boolean }
/** One skin's painters over a page: dressing, re-tinting, undressing, and what an added node gets. */
export interface SessionSkin {
  /** Before the first wear: the page as the skin needs it (aqua wears Blue). */
  enter?: () => void;
  /** Everything the skin paints, in `look`. */
  wear: (look: SkinLook) => void;
  /** The accent moved on the same face: the parts that follow it. */
  tint: (look: SkinLook) => void;
  /** Everything put back as it was. */
  shed: () => void;
  /** After the last shed: the page given back (aqua returns the accent). */
  leave?: () => void;
  /** A node added while the skin is on. */
  dress: (node: Element, look: SkinLook) => void;
  /** Listeners the skin holds, released. */
  dispose?: () => void;
}
/** Watches `doc` for the session; a second install disposes the first. Returns the disposer. */
export declare const installSkins: (doc?: Document, win?: Window & typeof globalThis) => () => void;

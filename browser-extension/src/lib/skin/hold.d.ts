// The header mark's hold on an extension page (browser twin: js/ui/logo/stageTrigger.js).
/** The show the hold opens for the page's accent, motion mode and face; null without the facades. */
export declare const heldShow: (win?: Window & typeof globalThis) => string | null;
/** The skin that show stamps: 'webcore', 'aqua' or null. */
export declare const heldSkin: (win?: Window & typeof globalThis) => 'webcore' | 'aqua' | null;
/** A press held on `wrap` for `holdMs` flips the skin the hold resolves to. */
export declare const wireSkinHold: (wrap: Element | null | undefined,
  opts?: { holdMs?: number; win?: Window & typeof globalThis }) => void;

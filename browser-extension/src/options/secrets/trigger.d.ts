import type { StagePoint } from '../../lib/logo/stageRules.js';
export declare const OFF_TOAST: string;
/** Every show this page can run: the browser's list without the image-painting pink one. */
export declare const PAGE_SHOWS: readonly string[];
export declare const showActive: (name: string) => boolean;
export declare const clearWay: (doc?: Document) => void;
export declare const activateShow: (name: string, origin?: StagePoint | null,
  opts?: { replace?: boolean }) => boolean;
export declare const setShow: (name: string, on: boolean, origin?: StagePoint | null) => void;
export declare const heldShow: () => string | null;
export declare const wireLogoHold: (wrap: Element | null | undefined, opts?: { holdMs?: number }) => void;

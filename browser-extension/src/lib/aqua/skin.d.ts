// The aqua skin's live half (browser twins: js/ui/aqua/toggle.js, logo.js, icons.js).
import type { SessionSkin, SkinLook } from '../skin/install.js';
/** The preset the word stores as the user's accent. */
export declare const AQUA_ACCENT: 'blue';
/** True while `<html data-skin="aqua">` is stamped. */
export declare const isAquaOn: (doc?: Document) => boolean;
/** The glyphs `selector` names at or under `root` in the glossy art (on), or put back as they were (off). */
export declare const swapAquaIcons: (root: ParentNode | null | undefined, on: boolean, look?: Partial<SkinLook>, selector?: string) => void;
/** The skin's painters over `doc`, as lib/skin/install.js drives them. */
export declare const makeAquaSkin: (doc: Document) => SessionSkin;

// The webcore skin's live half (browser twin: js/ui/webcore/toggle.js).
import type { SessionSkin } from '../skin/install.js';
/** True while `<html data-skin="webcore">` is stamped. */
export declare const isSkinOn: (doc?: Document) => boolean;
/** The skin's painters over `doc`, as lib/skin/install.js drives them. */
export declare const makeWebcoreSkin: (doc: Document) => SessionSkin;

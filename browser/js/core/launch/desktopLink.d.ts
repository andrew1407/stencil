// The stencil:// link a desktop hand-off opens: a server reference or the picture inline,
// plus an optional .stc and its mode.
import type { LaunchPayload, LaunchScriptMode } from './deepLink.js';

export declare const INLINE_WARN_CHARS: number;
export declare const INLINE_MAX_CHARS: number;

export interface DesktopLaunchFields {
  scheme?: string;
  /** openInLaunchPayload's shape, or null for a script with no picture. */
  payload?: LaunchPayload | null;
  incognito?: boolean;
  script?: string;
  scriptMode?: LaunchScriptMode;
}

export declare const desktopLaunchUrl: (fields?: DesktopLaunchFields) => string;
/** Only a link carrying bytes or a script inline is measured against the OS launch limits. */
export declare const inlineVerdict: (url: string,
  opts?: { payload?: LaunchPayload | null; script?: string }) => 'refuse' | 'warn' | 'ok';

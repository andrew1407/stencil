// Shape of launch.js — the `stencil://` link the desktop app opens.
import type { LaunchPayload } from '../web/launch.js';

export type ScriptMode = 'open' | 'run';
export interface DesktopLinkParts {
  src?: string; layout?: Record<string, unknown> | string; incognito?: boolean;
  script?: string; scriptMode?: ScriptMode;
}
export declare const DESKTOP_SCHEME: string;
export declare const MAX_DESKTOP_LINK: number;
export declare const buildDesktopUrl: (parts?: DesktopLinkParts) => string;
export declare const desktopLaunch: (launch: LaunchPayload | null,
  opts?: { mode?: ScriptMode; incognito?: boolean }) => string;
export declare const isTooBigForDesktop: (url: unknown) => boolean;

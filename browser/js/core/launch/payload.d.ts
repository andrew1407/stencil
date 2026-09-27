// The `#stencil=` hand-off payload: a server reference for a linked session (the receiver
// re-fetches — no bytes, no token), else the inline image plus the full layout.
import type { DrawingApp } from '../drawingApp.js';
import type { LaunchPayload } from './deepLink.js';

/** `id` hands off a SAVED project instead of the open one; null when nothing is stored. */
export declare const openInLaunchPayload: (
  app: DrawingApp,
  opts?: { incognito?: boolean; id?: string | null },
) => LaunchPayload | null;
/** The same, its image a data URL: a stored project's object URL is read back first. */
export declare const openInLaunchPayloadResolved: (
  app: DrawingApp,
  opts?: { incognito?: boolean; id?: string | null },
) => Promise<LaunchPayload | null>;

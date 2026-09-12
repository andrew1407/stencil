// Shapes for background/handlers/dropZones.js — the on-page 4-quadrant drop overlay
// and what a dropped row does, reusing the same hand-off machinery as the page relays.

/** Keyed by the `MSG.DROPZONES_ARM` / `_DISARM` / `MSG.PAGE_DROP` channel names. */
export declare const dropZoneHandlers: Record<string, (msg: Record<string, unknown>, sender?: { tab?: { id?: number; url?: string } }) => void>;

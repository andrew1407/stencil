// Shapes for options/pinRow.js — one row of the pinned-images list.
import type { Pin } from './pinsDom.js';

/** `serverSources` is the set of pin `source` URLs also stored on a connected server. */
export declare const renderPinRow: (pin: Pin, serverSources: Set<string> | null) => HTMLLIElement;

// A peer's edit on the picture this editor already holds: when the server's original is the one on
// screen, its layout — crop and turn included — is adopted in place as one undo step. The original
// is not re-downloaded and the undo history before it survives.
import type { DrawingApp } from '../drawingApp.js';
import type { LayoutPayload } from '../layout.js';
import type { RemoteSyncController } from './syncController.js';

/** The server record fields that name its picture, as one comparable string ('' when it has no originalHash). */
export declare const imageSignature: (rec: {
  hasImage?: boolean; originalHash?: string; blankColor?: string;
} | null | undefined) => string;
/** The record names the noted picture: both carry the original's hash and it is equal. */
export declare const samePicture: (knownSig: string, rec: Parameters<typeof imageSignature>[0]) => boolean;
/** The server's originalHash for these bytes (SHA-256, lowercase hex); '' where WebCrypto is unavailable. */
export declare const originalHashOf: (bytes: BufferSource) => Promise<string>;
/** The signature with the original swapped for this editor's upload, the noted blank fill kept. */
export declare const withOriginal: (sig: string, hash: string) => string;
/** Adopts the layout in place — lines, filter, page, formulas, crop and turn — as one undo step when any
 * of them moved; false when it is invalid or names no crop rect (the caller reloads). */
export declare const applyPeerLayout: (app: DrawingApp, layout: LayoutPayload,
  remote: Pick<RemoteSyncController, 'adoptServerFilter' | 'adoptServerPageFormat' | 'adoptServerFormulas'>) => boolean;

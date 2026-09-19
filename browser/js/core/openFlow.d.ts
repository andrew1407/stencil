// Putting a new image in front of the user: opening one in this editor (optionally creating
// it on a server), resetting to a blank editor, and replacing the active project's image.
import type { DrawingApp } from './drawingApp.js';

/** Provenance, crop and drop-point options as loadImageFromFile and a launch both take them. */
export interface OpenOpts {
  crop?: { x: number; y: number; width: number; height: number } | null;
  noCrop?: boolean;
  source?: string | null;
  resource?: string | null;
  landing?: boolean;
  from?: { x: number; y: number } | null;
}

/** An explicit `crop` wins, else `noCrop` takes the whole frame; returns the mutated target. */
export declare const applyOpenOpts: <T extends Record<string, unknown>>(target: T, opts: OpenOpts) => T;
/** `keepChat` forwards to newTemporary, for a reset made mid-turn. */
export declare const newEditor: (app: DrawingApp, opts?: { keepChat?: boolean }) => void;
/** `address` creates+links the project there, but incognito wins over a server target. */
export declare const openImageHere: (app: DrawingApp, file: File | Blob | null,
  incognito?: boolean, address?: string | null, opts?: OpenOpts) => void;
/** Same project id and server link; `crop` is a rect in the NEW image's pixels. */
export declare const replaceProjectImage: (app: DrawingApp, file: File | Blob | null,
  opts?: { rename?: boolean; keepAnnotations?: boolean; crop?: OpenOpts['crop'] }) => void;
/** Reserves the server for the blank the next open will create WITH real bytes. */
export declare const createRemoteBlank: (app: DrawingApp, address: string) => Promise<{ address: string }>;

// The page leaving or hiding: the trailing save, the co-edit result and the thumbnail land now,
// on `pagehide` and on a hidden `visibilitychange` as well as `beforeunload`.
import type { DrawingApp } from '../../core/drawingApp.js';

/** Idempotent: each flush is a no-op when nothing is pending. */
export declare const flushPendingWrites: (app: DrawingApp) => void;
export declare const installUnloadFlush: (app: DrawingApp, win?: Pick<Window, 'addEventListener'>,
  doc?: Pick<Document, 'addEventListener' | 'visibilityState'>) => void;

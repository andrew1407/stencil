// The second half of the load flow, run once the original has decoded: rotation + crop,
// which lines survive, the session commit, then the server push a replace-in-place or a
// create-on-server owes.
import type { DrawingApp } from './drawingApp.js';
import type { LoadImageOptions, LoadPlan } from './imageLoadFlow.js';

export declare const settleLoadedImage: (
  app: DrawingApp, file: File, opts: LoadImageOptions, plan: LoadPlan,
) => Promise<void>;

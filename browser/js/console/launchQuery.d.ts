// Applies the launch query (core/launch/query.js) through window.stencil once at boot, after the
// `#stencil=` hand-off: look, assistant, servers, picture, layout, project, chat, in that order.
import type { DrawingApp } from '../core/drawingApp.js';
import type { LaunchQuery } from '../core/launch/query.js';
import type { Stencil } from './stencilApi.js';

/** Applies `query`, or the app's own launch query (then strips the URL at once); every problem is one notice, and the promise never rejects. */
export declare const applyLaunchQuery: (ctx: { app: DrawingApp; stencil: Stencil; query?: LaunchQuery | null }) => Promise<void>;

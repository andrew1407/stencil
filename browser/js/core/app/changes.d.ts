// The editor's change feed: an edit names the inputs it moved, never the controls to repaint.
// ui/control/state.js subscribes each control area to the channels it reads, and FLUSH runs every
// area one signal dirtied, once. `app.changes` is the Emitter (core/emitter.js) the app is wired with.
import type { Emitter } from '../emitter.js';

export type ChangeTopic = 'history' | 'lines' | 'selection' | 'drawing' | 'compare' | 'project';

/** The narrow channels, by name. */
export declare const CHANGE: Readonly<Record<ChangeTopic, ChangeTopic>>;
/** Emitted after a signal's topics: every area they dirtied runs once. */
export declare const FLUSH: 'flush';
/** Emits each topic on `app.changes`, then FLUSH; a no-op without a feed. */
export declare const changed: (app: { changes?: Emitter | null }, ...topics: ChangeTopic[]) => void;

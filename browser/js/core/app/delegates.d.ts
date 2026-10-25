// How DrawingApp takes on methods it does not write in its class body: the view seam
// (drawingApp.js VIEW_SEAM, the ui/ functions the core and the facade reach through the app).
// Every core function is imported and called by its callers.

/** A seam function: the app first, then the method's own arguments. */
export type AppFunction = (app: never, ...args: never[]) => unknown;

/** Writes each `name → fn` onto `proto` as a non-enumerable, named method handing `this` first. */
export declare const installDelegates: (proto: object, functions: Readonly<Record<string, AppFunction>>) => void;

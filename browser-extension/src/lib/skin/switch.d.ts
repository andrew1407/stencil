// A session skin's switch over window.StencilSkin (browser twin: js/ui/logo/stageTrigger.js SKIN).
/** The skin `<html data-skin>` carries, or null. */
export declare const skinOn: (doc?: Document) => 'webcore' | 'aqua' | null;
/** Flips `name` through the facade; the state it is left in, false without the facade. */
export declare const toggleSkin: (name: string, win?: Window & typeof globalThis) => boolean;

// The CLI logo's accent actions: the preset cycle and the random vivid colour.
export declare const cycleAccent: (app: object, origin?: Element | null) => unknown;
/** `#rrggbb` of HSV (hue, 0.7, 0.95). */
export declare const vividHex: (hue: number) => string;
export declare const randomVividAccent: (app: object, origin?: Element | null, random?: () => number) => unknown;

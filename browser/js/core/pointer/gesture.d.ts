// The pointer gesture in flight, one of GESTURES: the seven exclusive drag and pan flags as one
// state, so two can never both be set and "is the pointer busy" is a single question.

export type Gesture = 'none' | 'pan' | 'point' | 'segment' | 'line' | 'zoomRect' | 'rectDraw' | 'compareSplit';

export declare const GESTURES: readonly Gesture[];
/** Each legacy flag and the gesture it names. */
export declare const GESTURE_FLAGS: Readonly<Record<string, Gesture>>;
/** Defines the flags as accessors over `this.gesture`: raising one ends any other. */
export declare const installGestureFlags: (proto: object) => void;
/** The gesture a host is in, read through its flags. */
export declare const activeGesture: (app: object) => Gesture;
/** Any gesture but the compare divider. */
export declare const canvasGestureActive: (app: object) => boolean;

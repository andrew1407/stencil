import type { GenieFlow, GenieRect } from './warp.js';

/** The tuned lengths (ms) at the default speed. */
export declare const GENIE_OPEN_MS: number;
export declare const GENIE_CLOSE_MS: number;
/** The class of the canvas a flight lays over the page. */
export declare const GENIE_CANVAS_CLASS: string;

/** Paints the warp at progress `p` (0 = at rest, 1 = on the icon) onto `ctx`, in CSS px. */
export declare function paintGenie(ctx: CanvasRenderingContext2D, shot: CanvasImageSource & { width: number; height: number },
                                   win: GenieRect, icon: GenieRect, side: GenieFlow, p: number): void;

/** The `transform` that keeps a fixed picture over `follow` as a layout moves it; '' while it stands. */
export declare const followDrift: (follow: Element | null | undefined) => () => string;

/** Plays the genie between `win` and `icon` (client rects) on a canvas over the page; returns `cancel()`. */
export declare function playGenie(shot: CanvasImageSource & { width: number; height: number }, win: GenieRect, icon: GenieRect,
                                  opts: { opening: boolean; ms: number; zIndex?: number; done?: () => void;
                                          /** A client rect the flight never paints outside. */ clip?: DOMRect | null;
                                          /** An element the flight rides with, moved by a layout mid-flight. */ follow?: Element | null }): () => void;

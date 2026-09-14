import type { TileCell, Speck } from './disintegrate.js';

export declare const MOTE_INK: number;
export declare const MOTE_RIM_INK: number;
/** Speck painter in `el`'s own surface colours (background lifted towards its ink). */
export declare function speckPainter(el: Element): (cell: TileCell) => Speck;

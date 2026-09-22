// A word typed into the bare window opens its logo show.
import type { DrawingApp } from '../../../core/drawingApp.js';

/** The letter a keydown spells: the layout's own Latin letter, else the US letter of its physical key. */
export declare const typedLetter: (e: Pick<KeyboardEvent, 'key' | 'code'>) => string;
/** The show a buffer ends with, or null. */
export declare const matchTypedWord: (buffer: string) => string | null;
export declare function wireTypedWords(app: DrawingApp, doc?: Document): void;

// A word typed into the bare window opens its logo show.
import type { DrawingApp } from '../../../core/drawingApp.js';

/** The letter a keydown spells: the layout's own Latin letter, else the US letter of its physical key. */
export declare const typedLetter: (e: Pick<KeyboardEvent, 'key' | 'code'>) => string;
/** The CLI mode's word, matched like a show's and opening the terminal instead. */
export declare const CLI_WORD: string;
/** The show a buffer ends with (CLI_WORD for the terminal), or null. */
export declare const matchTypedWord: (buffer: string) => string | null;
export declare function wireTypedWords(app: DrawingApp, doc?: Document): void;

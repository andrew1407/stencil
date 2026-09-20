// A word typed into the bare window opens its logo show.
import type { DrawingApp } from '../../core/drawingApp.js';

/** The show a buffer ends with, or null. */
export declare const matchTypedWord: (buffer: string) => string | null;
export declare function wireTypedWords(app: DrawingApp, doc?: Document): void;

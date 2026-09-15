// Shape of jsSource.js — which JavaScript buffers drive the Stencil facade.
export interface MarkerSpan { start: number; end: number }
export declare const JS_LANGUAGE: string;
export declare const MARKER_DOC: string;
export declare const MARKER_TRAILING_DOC: string;
export declare const inLineComment: (lineText: string, character: number) => boolean;
export declare const isJsDocument: (document: unknown) => boolean;
export declare const isJsSource: (document: unknown) => boolean;
export declare const markerLine: (document: unknown) => number;
export declare const markerSpan: (lineText: string) => MarkerSpan | null;
export declare const markerWordsAt: (lineText: string, character: number) => MarkerSpan | null;
export declare const marksStencil: (document: unknown) => boolean;

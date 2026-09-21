// Shape of pySource.js — which Python buffers drive pystencil.
export declare const PY_LANGUAGE: string;
export declare function markerSpan(lineText: string): { start: number; end: number } | null;
export declare function markerLine(document: unknown): number;
export declare function marksStencil(document: unknown): boolean;
export declare function isPyDocument(document: unknown): boolean;
export declare function isPySource(document: unknown): boolean;

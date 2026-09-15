// Shape of apiVocabulary.js — the window.stencil facade as the editor explains it.
export interface ApiEntry {
  group: string; signature: string; summary: string; detail?: string; readOnly?: boolean;
}
export declare const FACADE: string;
export declare const FACADE_DOC: string;
export declare const MEMBERS: Readonly<Record<string, ApiEntry>>;
export declare const MEMBER_NAMES: readonly string[];
export declare const entryFor: (name: string) => ApiEntry | undefined;
export declare const explain: (name: string) => string;
export declare const facadeAt: (lineText: string, character: number) => boolean;
export declare const groupOf: (name: string) => string | undefined;
export declare const memberAt: (lineText: string, character: number) => string;
export declare const namesInGroup: (group: string) => string[];
export declare const prefixAt: (linePrefix: string) => string | null;

// Shape of jsHints.js — window.stencil completions and hovers in JavaScript buffers.
export declare const SELECTOR: readonly { language: string }[];
export declare const completionProvider: {
  provideCompletionItems(document: unknown, position: unknown): unknown[];
};
export declare const hoverProvider: { provideHover(document: unknown, position: unknown): unknown };
export declare const itemsFor: (linePrefix: string) => unknown[];
export declare const typescriptAnswers: (document: unknown) => boolean;
export declare const register: (context: unknown) => unknown;

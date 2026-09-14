// Shape of completionContext.js — which suggestion groups belong at a caret.
export interface CompletionContext { groups: string[]; numberStem: string | null }
export declare const AFTER: Record<string, readonly string[]>;
export declare const AFTER_USE: Record<string, readonly string[]>;
export declare const splitPrefix: (linePrefix: string) => { words: string[]; partial: string };
export declare const groupsFor: (words: string[]) => string[];
export declare const contextFor: (linePrefix: string) => CompletionContext;

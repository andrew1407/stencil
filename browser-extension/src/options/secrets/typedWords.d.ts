export declare const isTypingTarget: (t: EventTarget | null) => boolean;
export declare const matchTypedWord: (buffer: string) => string | null;
export declare const typedLetter: (e: { key?: string; code?: string }) => string;
export declare function wireTypedWords(doc?: Document): void;

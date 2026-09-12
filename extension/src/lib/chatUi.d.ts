export declare const AUTO_DISMISS_MS: number;
export interface Dismissible { dismiss(): void; button: HTMLButtonElement; }
export declare const makeDismissible: (el: HTMLElement, opts?: { doc: Document; autoMs?: number;
  timer?: typeof setTimeout; onDismiss?: () => void }) => Dismissible;

export declare const hideThumbPreview: () => void;
export interface ThumbPreview { show(e?: MouseEvent): void; hide(): void; }
export declare const wireThumbPreview: (img: HTMLImageElement, opts?: { doc?: Document; caption?: string;
  src?: string }) => ThumbPreview | undefined;

export declare const shrinkWrapWidth: (lineWidths: number[]) => number | null;
export declare const applyShrinkWrap: (el: HTMLElement, doc?: Document) => void;
export declare const bindShrinkWrapResize: (transcript: HTMLElement, selector?: string, doc?: Document) => void;

export interface Suggestion { label: string; prompt: string; }
export declare const SUGGESTIONS: Suggestion[];
export declare const renderSuggestions: (doc: Document, onPick: (prompt: string) => void,
  items?: Suggestion[]) => HTMLDivElement;

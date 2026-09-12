export interface ChatSuggestion { prompt: string; label: string; }

export declare const CHAT_SUGGESTIONS: ChatSuggestion[];
export declare const chatSuggestionsHtml: () => string;
export declare const typingDots: () => HTMLElement;
export declare const chatDropCueHtml: (id?: string) => string;
export declare const chatEmptyState: () => HTMLElement;

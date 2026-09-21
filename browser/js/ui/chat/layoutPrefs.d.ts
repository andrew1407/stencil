export declare const CHAT_SIDE_NORMAL: string;
export declare const CHAT_SIDE_SWAPPED: string;
export declare const CHAT_SWAPPED_CLASS: string;

export declare const chatSide: () => string;
export declare const setChatSide: (next: string) => void;
/** Flips the preference for the rest of this tab's session; returns the NEW side. */
export declare const toggleChatSide: () => string;
export declare const applyChatSide: (transcriptEl: Element | null | undefined, side?: string) => void;

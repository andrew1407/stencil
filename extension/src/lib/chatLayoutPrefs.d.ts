export declare const CHAT_SIDE_NORMAL: 'normal';
export declare const CHAT_SIDE_SWAPPED: 'swapped';
export declare const CHAT_SWAPPED_CLASS: 'chat-swapped';
export type ChatSide = typeof CHAT_SIDE_NORMAL | typeof CHAT_SIDE_SWAPPED;
export declare const chatSide: () => ChatSide;
export declare const setChatSide: (next: string) => void;
export declare const toggleChatSide: () => ChatSide;
export declare const applyChatSide: (transcriptEl: Element | null, side?: ChatSide) => void;

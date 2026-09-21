export declare function escapeHtml(v: unknown): string;
/** Whether `s` is a key combo and nothing else. */
export declare function isKeyCombo(s: unknown): boolean;
export declare function keysHtml(combo: string, mac?: boolean): string;
export declare function highlightKeys(text: unknown, mac?: boolean): string;
export declare function sentenceCase(s: unknown): string;

export type TipBlock =
  | { kind: 'row'; term: string; desc: string }
  | { kind: 'bullet'; text: string }
  | { kind: 'text' | 'hint' | 'note'; text: string; muted?: boolean };

export interface ParsedTip { title: string; keys: string[]; blocks: TipBlock[]; }

/** Parse a composed `title` string into {title, keys, blocks} in source order. */
export declare function parseTip(text: unknown): ParsedTip;
export declare function renderTip(text: unknown, mac?: boolean): string;

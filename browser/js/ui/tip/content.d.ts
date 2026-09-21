export declare const escapeHtml: (v: unknown) => string;

/** Whether `s` is a key combo and nothing else. */
export declare const isKeyCombo: (s: unknown) => boolean;
/** Render one already-validated combo as `<kbd>` keycaps joined by "+". */
export declare const keysHtml: (combo: string, mac?: boolean) => string;
/** Escape `text` and put keycaps on the key combos found inside it. */
export declare const highlightKeys: (text: unknown, mac?: boolean) => string;
/** Uppercase a leading lowercase prose word; leaves a value (filename, URL, single token) alone. */
export declare const sentenceCase: (s: unknown) => string;

export interface TipBlock {
  kind: 'row' | 'bullet' | 'text' | 'hint' | 'note';
  text?: string;
  term?: string;
  desc?: string;
  muted?: boolean;
}
export interface ParsedTip { title: string; keys: string[]; blocks: TipBlock[] }

/** Parse a composed `title` string into {title, keys, blocks}. */
export declare const parseTip: (text: unknown) => ParsedTip;
/** Render a composed `title` as the rich tooltip's HTML; '' when there is nothing to show. */
export declare const renderTip: (text: unknown, mac?: boolean) => string;

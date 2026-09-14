// Shape of vocabulary.js — the words the editor offers and the Markdown that explains them.
export interface VocabularyEntry {
  group?: string;
  signature?: string;
  summary: string;
  detail?: string;
  example?: string;
}
export declare const DIRECTIVES: Record<string, VocabularyEntry>;
export declare const WORDS: Record<string, VocabularyEntry>;
export declare const UNITS: Record<string, VocabularyEntry>;
export declare const DIRECTIVE_NAMES: readonly string[];
export declare const UNIT_NAMES: readonly string[];
export declare const MODES: readonly string[];
export declare const STYLES: readonly string[];
export declare const CROP_KEYS: readonly string[];
export declare const groupOf: (directive: string) => string | undefined;
export declare const entryFor: (word: string) => VocabularyEntry | undefined;
export declare const markdownFor: (label: string, entry?: VocabularyEntry) => string;
export declare const explain: (word: string) => string;

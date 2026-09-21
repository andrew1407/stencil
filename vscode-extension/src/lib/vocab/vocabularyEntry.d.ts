// Shape of vocabularyEntry.js — one entry's Markdown, and the explain each table is built from.
export interface VocabularyEntry {
  group?: string;
  signature?: string;
  summary: string;
  detail?: string;
  example?: string;
  readOnly?: boolean;
}
export declare const markdownFor: (label: string, entry?: VocabularyEntry, fence?: string) => string;
export declare const makeExplain: (spec: {
  normalize?: (word: string) => string;
  lookup: (key: string) => VocabularyEntry | undefined;
  label: (key: string, entry: VocabularyEntry) => string;
  fence?: string;
  decorate?: (markdown: string, entry: VocabularyEntry) => string;
}) => (word: string) => string;

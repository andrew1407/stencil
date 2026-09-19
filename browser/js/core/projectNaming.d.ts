// Project names and keywords over a plain array of registry rows: the duplicate-name guard,
// the copy-suffix numbering, the "same image" match and the keyword normalizer.
import type { ProjectMeta } from './projectsStore.js';

/** Trim, drop blanks, dedupe case-insensitively (first-seen order) — the server's joinKeywords. */
export declare const normalizeKeywords: (keywords: unknown) => string[];
/** Strip a trailing copy suffix " (N)". */
export declare const baseProjectName: (name: unknown) => string;

export declare const nameExists: (metas: readonly ProjectMeta[], name: unknown, exceptId?: string | null) => boolean;
/** Gates the rename ✓ button; `exceptId` is the project being renamed. */
export declare const validateName: (metas: readonly ProjectMeta[], name: unknown, exceptId?: string | null) =>
  { ok: boolean; reason: string };
/** Identical non-empty `source`, else a base-name match. */
export declare const findByImage: (metas: readonly ProjectMeta[], source: string | null | undefined, name: string | null | undefined) => ProjectMeta[];
/** The bare base name when free, else the lowest unused "Name (N)". */
export declare const copyName: (metas: readonly ProjectMeta[], baseName: string | null | undefined, source: string | null | undefined) => string;
/** "Untitled N", one past the highest already taken. */
export declare const defaultName: (metas: readonly ProjectMeta[]) => string;

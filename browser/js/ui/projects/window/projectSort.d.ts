export type SortMode = 'name' | 'local' | 'server' | 'date-desc' | 'date-asc' | 'manual';
export interface SortableItem { key: string; name: string; date: number; isRemote: boolean }

export declare const SORT_MODES: SortMode[];

/** Returns a new, sorted array; `order` is the manual key sequence (mode 'manual' only). */
export declare const sortProjectItems: <T extends SortableItem>(items: T[], mode: SortMode, order?: string[]) => T[];

/** Seed from `base`, give every current key a slot, then move draggedKey before/after targetKey. */
export declare const reconcileManualOrder: (
  fullKeys: string[], base: string[] | null | undefined, draggedKey: string, targetKey: string, before: boolean,
) => string[];

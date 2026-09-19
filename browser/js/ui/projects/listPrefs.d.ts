export interface ListPrefs {
  sortMode(): string;
  setSortMode(mode: string): void;
  setSearchMode(mode: string): void;
  /** Put the remembered choices back on the two selects. */
  syncControls(): void;
  /** Does this row match the query, in the current search scope? */
  matchRow(name: string, keywords: readonly string[] | null | undefined, q: string): boolean;
  /** The manual drag order, as a list of row keys. */
  loadOrder(): string[];
  saveOrder(order: readonly string[]): void;
}

export declare function createListPrefs(els: {
  sortEl: HTMLSelectElement | null;
  searchModeEl: HTMLSelectElement | null;
}): ListPrefs;

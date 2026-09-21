/** One checked row: a local project, or a project that lives on a server. */
export interface SelectionEntry {
  kind: 'local' | 'remote';
  id: string;
  serverUrl: string | null;
  /** A local meta with a remoteId + address is server-BACKED; a pure-local one is not. */
  isServer: boolean;
  meta: Record<string, unknown>;
}

export interface ProjectSelection {
  /** Row key → entry, for every CHECKED row. */
  selected: Map<string, SelectionEntry>;
  /** Keys whose removal dust is still playing: on screen, already gone to the bar. */
  doomed: Set<string>;
  /** What THIS render offered a checkbox for — the select-all pool. */
  selectables: Map<string, SelectionEntry>;
  batchBtns: Record<string, HTMLElement>;
  sel(): SelectionEntry[];
  /** Retire a key for its scatter; the returned undo runs once the dust has landed. */
  retireKey(key: string): () => void;
  localKey(id: string): string;
  remoteKey(meta: { serverUrl: string; id: string }): string;
  isServerMeta(meta: Record<string, unknown> | null): boolean;
  anyLiveSelectable(): boolean;
  allSelected(): boolean;
  updateBatchBar(): void;
  updateSelectAll(): void;
  clearSelection(): void;
  toggleSelect(key: string, entry: SelectionEntry, on: boolean): void;
}

export function createProjectSelection(deps: {
  batchBar: HTMLElement; batchCount: HTMLElement; hasServers(): boolean;
}): ProjectSelection;

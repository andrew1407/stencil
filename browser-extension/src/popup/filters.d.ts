// Shapes for popup/filters.js — reads lib/filterUi.js's controls, ranks and re-renders
// the list. `filters` is the last read() result, kept live for row.js's own re-filter.
export interface PopupFilters {
  search: string;
  regex: boolean;
  formats: string[];
  minW: number | null;
  maxW: number | null;
  minH: number | null;
  maxH: number | null;
  includeImg: boolean;
  includeBg: boolean;
  includeVideo: boolean;
  includePosters: boolean;
  includeMeta: boolean;
}

export interface FilterUi {
  read(): PopupFilters;
  save(): void;
  load(): Promise<void>;
  restoreStatic(): void;
  populateFormats(items: unknown[]): void;
  applyPersistedFormats(): void;
  checkboxes(): HTMLInputElement[];
  allChecked(): boolean;
  updateToggleLabel(): void;
  acceptExternal(nv: unknown): boolean;
}

export declare const filterUi: FilterUi;
export declare let filters: PopupFilters;
export declare const renderCount: () => void;
export declare const applyFilters: () => void;

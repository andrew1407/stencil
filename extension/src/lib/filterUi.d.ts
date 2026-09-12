export declare const COMMON_FORMATS: string[];
export declare const FILTERS_KEY: string;

export interface ScanItem { kind?: string; src?: string; videoUrl?: string; [key: string]: unknown; }
export interface FormatList { formats: string[]; present: Set<string>; }
export declare const formatListFor: (items: ScanItem[]) => FormatList;
export declare const formatPillsHtml: (formats: string[], present: Set<string>) => string;

export interface Filters {
  search: string; regex: boolean; formats: string[]; minW: number | null; maxW: number | null;
  minH: number | null; maxH: number | null; includeImg: boolean; includeBg: boolean;
  includeVideo: boolean; includePosters: boolean; includeMeta: boolean;
}
export interface FilterUi {
  read(): Filters;
  save(): void;
  load(): Promise<void>;
  restoreStatic(): void;
  populateFormats(items: ScanItem[]): void;
  applyPersistedFormats(): void;
  checkboxes(): HTMLInputElement[];
  allChecked(): boolean;
  updateToggleLabel(): void;
  acceptExternal(nv: Record<string, unknown> | null): boolean;
}
export declare const createFilterUi: (opts?: { doc?: Document; onChange?: () => void }) => FilterUi;

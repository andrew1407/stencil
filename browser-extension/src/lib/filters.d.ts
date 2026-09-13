export declare const UNKNOWN_FORMAT: string;
export declare const VIDEO_FORMATS: string[];
export declare const formatOf: (src: string) => string;

export interface FormatItem { kind?: string; src?: string; videoUrl?: string; }
export declare const formatOfItem: (item: FormatItem | null | undefined) => string;
export declare const distinctFormats: (items: FormatItem[]) => string[];
export declare const extractCssUrls: (bg: string | null | undefined) => string[];

export interface SearchFilter { search?: string; regex?: boolean; }
export declare const matchesSearch: (item: { name?: string; src?: string; videoUrl?: string },
  f?: SearchFilter) => boolean;

export interface PassFilter extends SearchFilter {
  formats?: string[]; minW?: number; maxW?: number; minH?: number; maxH?: number;
  includeImg?: boolean; includeBg?: boolean; includeVideo?: boolean; includePosters?: boolean; includeMeta?: boolean;
}
export interface FilterableItem {
  kind?: string; name?: string; src?: string; videoUrl?: string; poster?: boolean; meta?: boolean;
  w?: number; h?: number;
}
export declare const passesFilters: (item: FilterableItem, f?: PassFilter) => boolean;

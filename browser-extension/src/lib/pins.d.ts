export interface PinEntry {
  source: string;
  site: string;
  resource: string;
  name: string;
  kind: string;
  t: number;
  color?: string;
  keywords?: string[];
}

export declare const PINS_KEY: string;
export declare const PIN_SEARCH_MODES: string[];

export declare function siteOf(url: string): string;
export declare function pinKey(site: string, source: string): string;
export declare function isPinnedIn(entries: PinEntry[], site: string, source: string): boolean;
export declare function matchPinsForSite(entries: PinEntry[], site: string): PinEntry[];
export declare function sitesOf(entries: PinEntry[]): string[];
export declare function normalizeKeywords(keywords: unknown[]): string[];
export declare function pinKeywords(pin: PinEntry | null | undefined): string[];
export declare function pinMatchesSearch(pin: PinEntry, query: string, mode?: string): boolean;
export declare function addPinEntry(
  entries: PinEntry[],
  rec: { source: string; site: string; resource?: string; name?: string; kind?: string; color?: string; keywords?: string[]; t?: number },
): PinEntry[];
export declare function projectNameColor(color: string, fallback: string): string;
export declare function removePinEntry(entries: PinEntry[], site: string, source: string): PinEntry[];
export declare function removeSiteEntries(entries: PinEntry[], site: string): PinEntry[];
export declare function loadPins(): Promise<PinEntry[]>;
export declare function setPinned(rec: {
  source: string; site?: string; resource?: string; name?: string; kind?: string; keywords?: string[]; pinned: boolean;
}): Promise<PinEntry[]>;
export declare function setPinKeywords(site: string, source: string, keywords: string[]): Promise<PinEntry[]>;
export declare function clearPins(site?: string): Promise<PinEntry[]>;

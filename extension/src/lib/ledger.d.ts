export interface LedgerEntry {
  source: string;
  resource: string;
  name: string;
  editorUrl: string;
  t: number;
  count: number;
}

export declare const LEDGER_KEY: string;
export declare const RECONCILE_GRACE_MS: number;

export declare function trackableSource(source: string): boolean;
export declare function matchEntries(entries: LedgerEntry[], source: string, name: string): LedgerEntry[];
export declare function loadLedger(): Promise<LedgerEntry[]>;
export declare function recordOpened(
  rec: { source: string; resource?: string; name?: string; editorUrl?: string; t?: number },
): Promise<LedgerEntry | null>;
export declare function lookup(source: string, name: string): Promise<LedgerEntry[]>;
export declare function originOf(url: string): string;
export declare function reconcileLedger(
  entries: LedgerEntry[], projects: Array<{ source?: string }>, editorOrigin: string, now?: number, graceMs?: number,
): LedgerEntry[];
export declare function pruneLedger(projects: Array<{ source?: string }>, editorOrigin: string): Promise<boolean>;

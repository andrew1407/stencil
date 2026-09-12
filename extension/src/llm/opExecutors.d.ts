// Shapes for llm/opExecutors.js — one executor per §8 op, keyed like the validators
// (opValidate.js OP_REGISTRY). Each mutates the shared execution context; every
// capability is injected, and a surface that lacks one warns instead of throwing.
import type { ListingItem } from './chatController.js';
import type { PlanAction } from './opPlan.js';

/** Carried across one plan's actions; `cards` is what the chat transcript renders. */
export interface ExecContext {
  listing: ListingItem[];
  tabs: unknown[];
  cards: Array<Record<string, unknown>>;
  warnings: string[];
  /** LlmImages fetched by `attach`, replayed as the §7 continuation turn's images. */
  attached: unknown[];
  attachedIndices: number[];
  scannedTab: { index: number; title: string; count: number } | null;
  rescanned: { count: number } | null;
}

export type OpExecutor = (action: PlanAction, ctx: ExecContext) => Promise<void>;

export interface OpExecutorCapabilities {
  history?: Array<{ role: string; text: string }>;
  focusImage?: (index: number, entry: ListingItem) => unknown;
  openImage?: (action: PlanAction, entry: ListingItem) => unknown;
  attachImage?: (index: number, entry: ListingItem) => unknown;
  pinImage?: (index: number, entry: ListingItem) => unknown;
  unpinImage?: (index: number, entry: ListingItem) => unknown;
  openUrlImage?: (action: PlanAction) => unknown;
  setTheme?: (mode: string) => unknown;
  setAccent?: (action: PlanAction) => unknown;
  setFilters?: (action: PlanAction) => unknown;
  scanTab?: (tab: unknown, index: number) => unknown;
  rescan?: () => unknown;
  /** Marks the turn-scoped §10 clear-chat request; resolved after every other action. */
  askClear?: () => void;
}

/** One executor per op name (focus, open, attach, pin, unpin, openUrl, theme, accent,
 * filter, scanTab, clearChat, rescan). */
export declare function createOpExecutors(capabilities?: OpExecutorCapabilities): Record<string, OpExecutor>;

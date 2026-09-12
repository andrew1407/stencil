// Shapes for popup/assistant/capabilities.js — what each whitelisted §8 op does on this
// surface. Builds llm/chatController.js's controller and keeps it on `state.controller`.
import type { ChatImage } from '../../llm/llmClient.js';

export interface CapabilitiesResult {
  attachImage(index: number, entry: unknown): Promise<ChatImage>;
  pinImage(index: number, entry: unknown): Promise<void>;
  unpinImage(index: number, entry: unknown): Promise<void>;
}

export declare function createCapabilities(opts: {
  getItems(): unknown[];
  getTabId(): number | null;
  getPageUrl(): string;
  openHere(entry: unknown, opts: unknown): Promise<boolean | void> | boolean;
  pinEntry?(entry: unknown): unknown;
  unpinEntry?(entry: unknown): unknown;
  rescanPage?(): unknown;
  setTheme?(mode: string): unknown;
  setFilters?(patch: unknown): unknown;
  setAccent?(action: unknown): unknown;
  cachedSettings(): Promise<unknown>;
  state: { llmSettings: unknown; workingScan: unknown; controller: unknown; wipeAfterTurn: boolean };
}): CapabilitiesResult;

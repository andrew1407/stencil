// Shapes for popup/assistant.js — the panel's "Assistant" section (llm-contract.md §8),
// composed from popup/assistant/* around llm/chatController.js.
export declare const applyAssistantVisibility: (
  enabled: unknown, opts?: { section?: HTMLElement | null; button?: HTMLElement | null },
) => boolean;

export interface AssistantController {
  /** A hidden section is not a collapsed one; boots the section lazily on its first reveal. */
  handleToggle(collapsed: boolean): void;
  reveal(): void;
}

export declare function createAssistant(opts: {
  getItems(): unknown[];
  getTabId(): number | null;
  getPageUrl(): string;
  openHere?(entry: unknown, opts: unknown): unknown;
  pinImage(entry: unknown): unknown;
  unpinImage(entry: unknown): unknown;
  rescan(): unknown;
  setTheme(mode: string): unknown;
  setFilters(patch: unknown): unknown;
  setAccent(action: unknown): unknown;
}): AssistantController;

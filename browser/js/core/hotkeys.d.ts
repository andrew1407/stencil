// The single hotkey registry: id → combo, defaults from config/hotkeysConfig.json
// (platformized: Meta-based on Mac), overrides merged from localStorage
// 'drawingApp_hotkeys'. DOM and storage access is guarded so a Node import stays inert.

export interface Hotkeys {
  readonly isMac: boolean;
  /** Current combo for an id, undefined when unknown. */
  get(id: string): string | undefined;
  getDefault(id: string): string | undefined;
  /** Does not persist — callers save() explicitly. */
  set(id: string, combo: string): void;
  reset(id: string): void;
  resetAll(): void;
  entries(): [string, string][];
  save(): void;
  /** "<label> (<combo>)", platform-formatted; just the label when the id has no binding. */
  hkTitle(label: string, id: string): string;
  /** Rewrites every `[data-hk]` keycap and `[data-hk-title]` tooltip to the live bindings. */
  updateCtxHints(): void;
  updateHotkeyTitles(): void;
}

export declare const hotkeys: Hotkeys;

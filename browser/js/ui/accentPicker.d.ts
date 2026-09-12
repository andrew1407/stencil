/** Preview hooks for a hover flood: repaint only, a click still commits. */
export interface AccentPreview {
  on(key: string): void;
  off(): void;
}

/** Fill `menu` with one row per accent; returns the dismiss-time restore (call on close). */
export declare function fillAccentMenu(
  menu: HTMLElement, onPick: (key: string) => void, preview?: AccentPreview | null,
): () => void;

/** Marks the row matching `value` aria-selected; a non-preset value selects nothing. */
export declare function markSelected(menu: HTMLElement, value: string | null): void;

export interface AccentPickerApi {
  set(key: string): void;
}

export declare function buildAccentPicker(
  mount: HTMLElement,
  opts: { current: string; onSelect?: (key: string) => void; preview?: AccentPreview | null },
): AccentPickerApi;

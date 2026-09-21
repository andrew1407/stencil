export interface EnhanceSelectOptions {
  search?: boolean;
  /** Returns an icon's HTML for one option value, or ''. */
  icons?: ((value: string) => string) | null;
  /** A canvas-only hover repaint; only a real pick commits, through the native `change`. */
  preview?: ((value: string) => void) | null;
}

/** Overlays a native <select> with a themed dropdown; the native element stays the source of truth. */
export declare function enhanceSelect(selectEl: HTMLSelectElement, opts?: EnhanceSelectOptions): void;

export declare function enhanceAllSelects(
  root?: ParentNode,
  opts?: { search?: string[]; preview?: ((selectEl: HTMLSelectElement) => ((value: string) => void) | null) | null },
): void;

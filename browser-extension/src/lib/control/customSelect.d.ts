// PORT of browser/js/ui/control/customSelect.js. Builds a styled trigger + menu over a native
// <select>, which stays the source of truth (wrapped `.value`, a bubbling `change`).
export interface EnhanceSelectOptions {
  search?: boolean;
  icons?: ((value: string) => string) | null;
  preview?: ((value: string) => void) | null;
}
export declare function enhanceSelect(selectEl: HTMLSelectElement, opts?: EnhanceSelectOptions): void;

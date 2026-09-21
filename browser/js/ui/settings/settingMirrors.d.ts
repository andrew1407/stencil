/** One bound element, as a setting descriptor lists it. */
export interface Mirror {
  /** An element id — or, for kind 'radio', the input[name] of a context-menu group. */
  id: string;
  kind: 'value' | 'valueSkipFocus' | 'checked' | 'checkIcon' | 'radio';
}

/** Reflect `value` into one bound element the way that element expects. */
export function applyMirror(m: Mirror, value: unknown): void;

export function paintTintControls(isCustom: boolean): void;
export function paintCustomSizeGroup(isCustom: boolean): void;
export function paintFormulaToggle(checked: boolean): void;
export function paintFormulaError(hasError: boolean): void;
export function paintTooltipOption(id: string, on: boolean): void;
export function paintMotionMode(mode: string): void;
export function paintMotionDrawing(on: boolean): void;

/** A control's trimmed text, '' when it is not mounted. */
export function readControl(id: string): string;

/** Run `fn` over each of these controls that is actually mounted. */
export function forEachControl(ids: string[], fn: (el: HTMLElement) => void): void;

/** Ticks the Visuals "Dim and blur behind windows" box from the store. */
export declare function paintMotionBackdrop(on: boolean): void;

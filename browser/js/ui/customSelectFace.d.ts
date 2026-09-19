export interface SelectFace {
  /** The span the native select, trigger and menu are re-homed into. */
  wrap: HTMLSpanElement;
  trigger: HTMLButtonElement;
  menu: HTMLUListElement;
  /** The trigger's label and icon slots. */
  cur: HTMLElement;
  curIcon: HTMLElement;
  /** Mirrors the native select's `disabled` onto the trigger. */
  syncDisabled: () => void;
  /** Floors the label at the widest option's width; a no-op once it has measured. */
  fitToWidestOption: () => void;
}

/** Builds the wrap, trigger and popup list for one enhanced <select>. */
export declare function buildSelectFace(selectEl: HTMLSelectElement): SelectFace;

/** The accessible name a control's tooltip implies; '' when it has none. */
export declare const controlLabel: (el: Element) => string;
/** Set `aria-label` on one control, unless it names itself in text or via aria-labelledby. */
export declare const labelControl: (el: Element | null) => void;
/** Point an unnamed field at the row label beside it, else at its own placeholder. */
export declare const nameField: (el: Element | null) => void;
/** Name every `[data-title]` control and every field under `root`. */
export declare const labelControls: (root?: ParentNode) => void;
/** Name the controls on the page and keep later or renamed ones named. Returns the observer. */
export declare const watchControlLabels: (root?: Element) => MutationObserver;

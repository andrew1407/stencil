// One page-wide subtree observer for every pass that upgrades what renders later — the numeric
// fields' expressions, the controls' accessible names — instead of one observer each.

/** `fn` sees every element added under `root` from now on, in registration order. */
export declare const onElementAdded: (fn: (el: Element) => void, root?: Node) => void;
/** `fn` sees every element whose data-title or data-tip changes. */
export declare const onTipChanged: (fn: (el: Element) => void, root?: Node) => void;

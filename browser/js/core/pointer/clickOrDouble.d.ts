// One control, two meanings: a click, or a double click inside the double-click window.

/** A handler running `single` after the double-click window, or `double` on a second click inside it. */
export declare const clickOrDouble: (on: { single: (e?: Event) => void; double: (e?: Event) => void }) => (e?: MouseEvent) => void;

/** Rate every CSS animation of `doc` not yet in `seen` at `rate`, infinite loops aside. */
export declare const rateCssAnimations: (doc: Document, rate: number, seen?: WeakSet<Animation>) => void;
/** Keep the stylesheets' motion at the Visuals Animation speed; returns the uninstall. */
export declare const installCssSpeed: (doc?: Document) => () => void;

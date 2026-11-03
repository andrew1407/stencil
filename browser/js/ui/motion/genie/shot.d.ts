/** `el` cloned with every computed style inline (ids dropped, field values and canvases kept), plus its pseudo-element rules. */
export declare function cloneStyled(el: Element): { clone: Element; css: string };

/** Resolves a canvas holding `el` as it stands, at `scale` device px per CSS px; null when it cannot be drawn. */
export declare function snapshot(el: Element | null | undefined, opts?: { scale?: number }): Promise<HTMLCanvasElement | null>;

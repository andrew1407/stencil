/** The heading line, without the "(shortcut)" hint or the "— reason" note. */
export declare function tipLabel(text: unknown): string;
export declare function setTip(
  el: Element | null | undefined, text: unknown, opts?: { label?: boolean },
): Element | null | undefined;

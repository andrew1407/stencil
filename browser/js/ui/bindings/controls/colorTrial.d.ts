// A native colour picker whose drag is a trial and whose close commits once.

export interface ColorTrialHandlers {
  /** Each `input` tick, and the page's next touch after opening: the value on screen now, null back at the opening colour. */
  onTry(value: string | null): void;
  /** The `change` that closes a pick on a new colour. */
  onCommit(value: string): void;
}

/** Wires `input`; the returned function opens its picker beside `anchor`, at the value `input` holds then. */
export declare const colorTrial: (input: HTMLInputElement, handlers: ColorTrialHandlers) => (anchor: Element) => void;

// The accent recolour wipe over the CLI stage (press.zig wipeRecolor).
export interface Recolour {
  /** Wipes the banner and the accented scrollback to `next`; at once under a restyled skin. */
  wipeTo: (next: string) => void;
  /** Settles a running wipe at once. */
  cancel: () => void;
  wiping: () => boolean;
  /** The accent the banner wears now (the old one while a wipe runs). */
  painted: () => string | null;
  setPainted: (accent: string) => void;
}
export declare const createRecolour: (opts: {
  host: HTMLElement; banner: HTMLElement; out: HTMLElement; prompt: HTMLElement; rules: HTMLElement[];
  skin: () => string | null; paint: (accent: string) => void; schedule?: (fn: () => void, ms: number) => unknown;
}) => Recolour;

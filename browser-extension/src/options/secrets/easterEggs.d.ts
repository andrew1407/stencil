export type EasterEggs = Record<string, unknown> & {
  close(): EasterEggs; what(): string[]; of(word: string): EasterEggs;
};
export declare const createEasterEggs: (doc?: Document, self?: (() => unknown) | null) => EasterEggs;
export declare const installStencilFacade: (win?: Window & typeof globalThis, doc?: Document) => { EasterEggs: EasterEggs };

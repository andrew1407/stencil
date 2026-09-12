// Shapes for popup/assistant/composerMenu.js — the composer's "…" overflow (attach /
// clear / settings / swap sides), sharing the row menu's fly-both-edges motion.
export declare function wireComposerMenu(opts: {
  transcriptEl: HTMLElement;
  gearTip: { hide(): void; wire(el: HTMLElement): void };
  queueFiles(files: File[]): Promise<void>;
  state: { busy: boolean };
}): void;

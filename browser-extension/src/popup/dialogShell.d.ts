// Shapes for popup/dialogShell.js — the panel's modal/popover shell: click-away and
// Escape both resolve `undefined` (cancel); the finished value is whatever `build` sent.
export declare function openPanelDialog(opts: {
  build: (finish: (value?: unknown) => void) => Element | Element[];
  anchor?: Element | null;
}): Promise<unknown>;

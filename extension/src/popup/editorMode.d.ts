// Shapes for popup/editorMode.js — the panel while it stands on the Stencil editor: wires
// editorList.js and sourceTabsList.js to the panel's clock and the browser's tab events.
import type { SourceTabChoice } from '../lib/editorTabs.js';
import type { ImportHereFn } from './editorImport.js';

export interface EditorModeApi {
  /** false on the DevTools panel, which has no editor sections. */
  available: boolean;
  isLiveEditor(tabId: number | null): Promise<boolean>;
  setEditorTab(tabId: number | null): void;
  sourceTabs(): SourceTabChoice[];
  importHere: ImportHereFn;
  refresh(): void;
}

export declare function createEditorMode(opts: {
  setStatus(text: string): void;
  run(fn: () => Promise<void> | void): Promise<void>;
  dismiss(): void;
  menu: unknown;
  onSourceTab(picked: SourceTabChoice[]): void;
  imageDataUrl(image: unknown): Promise<string>;
  preview: unknown;
}): EditorModeApi;

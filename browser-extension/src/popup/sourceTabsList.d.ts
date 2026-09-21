// Shapes for popup/sourceTabsList.js — "Images from another page": a multi-select of
// open tabs, ticked pages included in editor mode's merged scan.
import type { SourceTabChoice } from '../lib/menu/editorTabs.js';

export interface SourceTabsController {
  render(): void;
  refresh(): Promise<void>;
  picked(): SourceTabChoice[];
  selectAll(): void;
  clearSelection(): void;
}

export declare function createSourceTabs(opts: {
  listedEl: HTMLElement;
  srcFilterEl?: HTMLInputElement | null;
  srcRegexEl?: HTMLInputElement | null;
  allBtn?: HTMLButtonElement | null;
  noneBtn?: HTMLButtonElement | null;
  noteEl: HTMLElement;
  ask(message: unknown): Promise<{ ok: boolean; error?: string; tabs?: SourceTabChoice[] }>;
  menu: { item(glyph: string, label: string, fn: () => unknown): HTMLElement; open(btn: HTMLElement, nodes: HTMLElement[]): void };
  setStatus(text: string): void;
  dismiss(): void;
  onSourceTab(picked: SourceTabChoice[]): void;
}): SourceTabsController;

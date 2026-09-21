// Shapes for popup/editorList.js — "Open editors": one row per open Stencil editor tab.
import type { EditorRow } from '../lib/menu/editorTabs.js';

export interface EditorListController {
  render(): void;
  refresh(): Promise<void>;
  clear(): void;
}

export declare function createEditorList(opts: {
  listEl: HTMLElement;
  searchEl: HTMLInputElement;
  regexEl: HTMLInputElement;
  ask(message: unknown): Promise<{ ok: boolean; error?: string; editors?: EditorRow[] }>;
  menu: { item(glyph: string, label: string, fn: () => unknown): HTMLElement; submenu(glyph: string, label: string, children: HTMLElement[]): HTMLElement; open(btn: HTMLElement, nodes: HTMLElement[]): void };
  setStatus(text: string): void;
  dismiss(): void;
  run(fn: () => Promise<void> | void): Promise<void>;
  preview: { bind(el: HTMLImageElement, small: string, bigger?: () => Promise<string>): void } | null;
  getEditorTabId(): number | null;
}): EditorListController;

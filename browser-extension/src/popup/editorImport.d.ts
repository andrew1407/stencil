// Shapes for popup/editorImport.js — importing one scanned row into the editor tab this
// panel stands on. Resolves false when nothing landed (the assistant then falls back to
// a new-tab hand-off).
import type { PopupImage } from './model.js';

export type ImportHereFn = (
  image: PopupImage,
  opts?: { incognito?: boolean; crop?: unknown; page?: string; anchor?: Element | null },
) => Promise<boolean>;

export declare function createImportHere(opts: {
  ask(message: unknown): Promise<{ ok: boolean; error?: string; needsChoice?: boolean; state?: unknown; projectName?: string }>;
  getEditorTabId(): number | null;
  setStatus(text: string): void;
  imageDataUrl(image: PopupImage): Promise<string>;
  refreshEditors(): Promise<void>;
  dismiss(): void;
}): ImportHereFn;

import type { DrawingApp } from '../../core/drawingApp.js';
import type { StencilElement } from '../base.js';
import type { RemoteProjectMeta } from '../../core/project/transferController.js';
import type { CopyScope, CopyOpen } from '../../core/project/copy/options.js';

/** What the confirmation copies: a stored row (id null = the live editor) or a server-only row. */
export interface CopyTarget {
  id?: string | null;
  remote?: RemoteProjectMeta | null;
  what: CopyScope;
  /** After the copy: the new local id, the server copy's remote id, or null for incognito. */
  onDone?: ((newId: string | null, open: CopyOpen) => void) | null;
}

/** "Make a copy": the local-copy and incognito boxes, and Just copy / Open in new tab / Open. */
export declare class StencilCopyProjectModal extends StencilElement {
  static inner(): string;
  static template(): string;
  /** Opens stacked over whatever is showing; `anchors` is the flight's two ends. */
  openFor(target: CopyTarget, anchors?: { from?: unknown; backTo?: unknown }): void;
  wire(app: DrawingApp): { open: (from?: unknown, backTo?: unknown, opts?: unknown) => void; close: () => void };
}

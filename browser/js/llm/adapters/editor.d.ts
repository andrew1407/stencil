// §2.1 editor adapters: saving the working image, and the composer nudge. A save promotes
// to a FRESH project id, so a multi-image plan leaves one project per image.
import type { DrawingApp } from '../../core/drawingApp.js';

export interface EditorAdapters {
  /** Send drained the queued attachments — every composer repaints its chips. */
  onAttachmentsChanged(): void;
  /** Persist the working image + layout as a local project; resolves to the unique name used. */
  saveProject(name?: string | null): Promise<string>;
}

export declare const editorAdapters: (app: DrawingApp) => EditorAdapters;

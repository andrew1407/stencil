// §2.1 editor adapters: saving the working image, and the composer nudge. A save promotes
// to a FRESH project id, so a multi-image plan leaves one project per image.
import type { DrawingApp } from '../../core/drawingApp.js';
import type { EditorMemento, Snapshot } from '../../core/historyStack.js';
import type { EditorHistory } from '../plan/sandbox.js';

/** The undo stack as it stood — its steps, cursor and floor — and the view on screen. */
export interface HistoryMark { steps: Snapshot[]; step: number; floor: Snapshot; memento: EditorMemento; }

export interface EditorAdapters {
  /** Send drained the queued attachments — every composer repaints its chips. */
  onAttachmentsChanged(): void;
  /** Persist the working image + layout as a local project; resolves to the unique name used. */
  saveProject(name?: string | null): Promise<string>;
  /** Rewinding puts the marked stack back and restores its view, persisting and repainting it. */
  editorHistory: EditorHistory<HistoryMark>;
}

export declare const editorAdapters: (app: DrawingApp) => EditorAdapters;

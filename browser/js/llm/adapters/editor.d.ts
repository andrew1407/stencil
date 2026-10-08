// §2.1 editor adapters: saving the working image, and the composer nudge. A save promotes
// to a FRESH project id, so a multi-image plan leaves one project per image.
import type { DrawingApp } from '../../core/drawingApp.js';
import type { EditorMemento, Snapshot } from '../../core/historyStack.js';
import type { EditorHistory } from '../plan/sandbox.js';

/** The undo stack as it stood — its steps, cursor and floor — the view on screen, and what was picked on it (selected line, multi-selection, points-table line). */
export interface HistoryMark { steps: Snapshot[]; step: number; floor: Snapshot; memento: EditorMemento; picked: [number, number[], number]; }

export interface EditorAdapters {
  /** Send drained the queued attachments — every composer repaints its chips. */
  onAttachmentsChanged(): void;
  /** Persist the working image + layout as a local project; resolves to the unique name used. */
  saveProject(name?: string | null): Promise<string>;
  /** Rewinding puts the marked stack back and restores its view and selection, persisting and repainting it. */
  editorHistory: EditorHistory<HistoryMark>;
}

export declare const editorAdapters: (app: DrawingApp) => EditorAdapters;

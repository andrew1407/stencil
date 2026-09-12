// Live two-way sync between a project and its linked .stencil file (Chromium File System
// Access only): debounced auto-save on edit plus a polled watch that applies external
// writes in place, or prompts mine/theirs/merge on a conflict.
import type { DrawingApp } from './drawingApp.js';

export type FileChangeKind = 'none' | 'local' | 'external' | 'conflict';
/** Pure classifier over the baseline ancestor, the editor's text and the file's text. */
export declare const classifyFileChange: (baseline: string | null, current: string, external: string | null) => FileChangeKind;

export declare class StencilSync {
  constructor(app: DrawingApp);
  app: DrawingApp;
  /** The linked FileSystemFileHandle; null = not file-linked. */
  handle: FileSystemFileHandle | null;
  /** The text last written/read — the sync common ancestor. */
  baseline: string | null;
  name: string;
  busy: boolean;
  lastMod: number | null;
  lastSize: number | null;
  readonly supported: boolean;
  readonly linked: boolean;
  /** Persisted opt-in; setting it starts/stops the poll. */
  liveSync: boolean;
  link(handle: FileSystemFileHandle, name?: string): Promise<void>;
  unlink(): void;
  /** Edit → debounced write-back. */
  onEdit(): void;
  flush(): Promise<void>;
  startPoll(): void;
  stopPoll(): void;
  check(): Promise<void>;
}

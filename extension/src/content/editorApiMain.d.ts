// Shapes for content/editorApiMain.js — `stencil.extension`, the frozen facade the editor
// page's console gets. Eight members; README.md carries the same table in prose.
// Every promise rejects with an Error whose message starts "Stencil: "; `open`'s
// already-holds-an-image rejection also carries `needsChoice` + `state`.
import type { EditorRow, SourceTabChoice } from '../lib/editorTabs.js';
import type { AttributedScanEntry } from '../lib/imageScan.js';
import type { EditorState, ImportMode } from '../lib/messages.js';

/** A scan entry with the two one-way shortcuts `images()` hangs off each row. */
export interface EditorScanEntry extends AttributedScanEntry {
  open(opts?: OpenOptions): Promise<OpenResult>;
  crop(opts?: { album?: boolean }): StencilExtensionApi;
}

/** A scan entry, an index into the last `images()` result, or a URL string. */
export type Target = EditorScanEntry | AttributedScanEntry | number | string;

export interface OpenOptions {
  tabId?: number;
  mode?: ImportMode;
  page?: string;
  crop?: unknown;
  incognito?: boolean;
  /** Provenance recorded with the import; defaults to the target's own. */
  resource?: string;
}

export interface OpenResult {
  tabId: number;
  mode: ImportMode;
  projectId: string;
  projectName: string;
}

/** The rejection `open` raises in its default 'ask' mode rather than overwrite work. */
export interface NeedsChoiceError extends Error {
  needsChoice: true;
  state: EditorState;
}

export interface StencilExtensionApi {
  /** One row per open editor tab; `thumbnails:false` skips the canvas capture. */
  editors(opts?: { thumbnails?: boolean }): Promise<EditorRow[]>;
  /** This tab's own state — answered by the bridge, without waking the worker. */
  readonly current: Promise<EditorState | null>;
  /** Activate a tab and raise its window (any tab, not just an editor's). */
  focus(tabId: number): Promise<{ tabId: number; windowId: number }>;
  /** Switch an editor tab to another of its OWN projects (default: this tab). */
  switchProject(projectId: string, opts?: { tabId?: number }): Promise<{ projectId: string; projectName: string }>;
  /** The open http(s) pages an image can be pulled from. */
  tabs(opts?: { currentWindowOnly?: boolean }): Promise<SourceTabChoice[]>;
  /** Scan another tab; the result is remembered, so `open(index)` indexes into it. */
  images(tabId: number, opts?: { limit?: number }): Promise<EditorScanEntry[]>;
  /** Import INTO an editor tab — no new tab, no navigation. */
  open(target: Target, opts?: OpenOptions): Promise<OpenResult>;
  /** One-way: the ordinary `#stencil=` hand-off in a NEW tab. Returns the facade. */
  openInNewTab(target: Target, opts?: { incognito?: boolean; resource?: string }): StencilExtensionApi;
  /** One-way: the quick-crop path. Returns the facade. */
  crop(target: Target, opts?: { album?: boolean }): StencilExtensionApi;
}

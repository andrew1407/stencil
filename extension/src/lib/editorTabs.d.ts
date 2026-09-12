// Shapes for lib/editorTabs.js — the questions editor mode asks of a plain chrome tab
// list. Every field is normalised, so the UI never null-checks.
import type { ImportMode } from './messages.js';

/** One "Open editors" row: a chrome tab joined with its EDITOR_STATE reply. */
export interface EditorRow {
  tabId: number | null;
  windowId: number | null;
  url: string;
  title: string;
  active: boolean;
  /** The tab the caller stands in — the default import target. */
  current: boolean;
  /** false = the bridge never answered; there is no preview and importing falls back to 'new'. */
  ready: boolean;
  projectId: string;
  projectName: string;
  hasImage: boolean;
  imageName: string;
  imageSize: { w: number; h: number } | null;
  /** Never persisted by the editor, so importing over it loses work with no copy on disk. */
  incognito: boolean;
  thumbnail: string;
  projects: Array<{ id: string; name: string; active: boolean }>;
}

/** One entry in "Images from another page": a scannable tab, labelled for the <select>. */
export interface SourceTabChoice {
  tabId: number;
  title: string;
  url: string;
  host: string;
  favIconUrl: string;
  /** "title — host", or whichever of the two exists. */
  label: string;
}

export declare function isEditorTab(url: string, editorUrl: string): boolean;
export declare function editorRow(tab: unknown, state: unknown, opts?: { currentTabId?: number | null }): EditorRow;
export declare function matchEditors(rows: EditorRow[], query: string, opts?: { regex?: boolean }): EditorRow[];
export declare function sourceTabChoices(
  tabs: unknown[],
  opts?: { editorUrl?: string; editorTabIds?: number[] | null },
): SourceTabChoice[];
export declare function matchSourceTabs(choices: SourceTabChoice[], query: string, opts?: { regex?: boolean }): SourceTabChoice[];
export declare function importModeFor(editorState: { hasImage?: boolean } | null | undefined): ImportMode;

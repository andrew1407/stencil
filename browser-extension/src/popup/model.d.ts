// Shapes for popup/model.js — the panel's live scan + UI state, and the merged local/
// shared image row every other popup module operates on.
import type { AttributedScanEntry } from '../lib/imageScan.js';

/** A scanned row, once annotated with pin/open state, or a server project standing in for one. */
export interface PopupImage extends AttributedScanEntry {
  pinned?: boolean;
  measured?: boolean;
  opened?: Array<{ count?: number }>;
  /** A row backed by a server project rather than a live page element. */
  shared?: boolean;
  serverUrl?: string;
  projectId?: string;
  source?: string;
  color?: string;
}

export interface PanelState {
  all: PopupImage[];
  filtered: PopupImage[];
  mode: 'page' | 'editor';
  sourceTabId: number | null;
  editorTabId: number | null;
  activeTabId: number | null;
  activeUrl: string;
  markOpened: boolean;
  openedFirst: boolean;
  showPinned: boolean;
  hoverHighlight: boolean;
  connections: unknown[];
  shared: PopupImage[];
  showServerPins?: boolean;
  sharedSources?: Set<string>;
  serverByOrigin?: Map<string, Set<string>>;
  serverHosts?: string[];
  openIn: { desktopScheme: string; telegramBotUsername: string };
}

export declare const state: PanelState;
export declare const surfaceTabId: () => number | null;
export declare const rowKey: (image: PopupImage) => string;
export declare const rowElFor: (image: PopupImage) => Element | null;
export declare const isPinned: (image: PopupImage) => boolean;
export declare const rowResource: (image: PopupImage) => string;
export declare const isProjectRow: (image: PopupImage) => boolean;
export declare const isOpened: (image: PopupImage) => boolean;

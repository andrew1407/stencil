// ── The panel's live state and the row predicates read off it. ───────────────
import { sourceOf } from '../lib/imageModel.js';
import { listEl } from './panelDom.js';

// `mode`: 'page' (classic — scan the tab we're on) or 'editor' (standing ON the Stencil
// editor: the editor sections show, the scan follows `sourceTabId`, rows import into it).
export const state = { all: [], filtered: [], mode: 'page', sourceTabId: null, editorTabId: null, activeTabId: null, activeUrl: '', markOpened: true, openedFirst: true, showPinned: true, hoverHighlight: false, connections: [], shared: [], openIn: { desktopScheme: 'stencil', telegramBotUsername: '' } };

// The tab the user is LOOKING at, which in editor mode is NOT the tab being scanned. Anything
// that mounts UI on a page (crop modal, editor modal, drop-zone overlay) must target this one
// or it lands on a background tab; reads of the scanned CONTENT keep `state.activeTabId`.
export const surfaceTabId = () => (state.mode === 'editor' ? state.editorTabId : state.activeTabId);

// Provenance/pin/search predicates (sourceOf / posterImage / editableSrc / pinnable /
// sharedMatchesSearch) live in ../lib/imageModel.js — pure + unit-tested.

// Stable identity for a row across renders — what the transition diffs. The source alone
// won't do: a server row and a local pin of the same image are different rows, and so are
// a video's poster and a plain image that happen to share a URL (each has its own toggle).
export const rowKey = (image) => (image.shared
  ? `server:${image.serverUrl}:${image.projectId || ''}:`
  : `page:${image.kind || ''}:${image.poster ? 'p' : ''}${image.meta ? 'm' : ''}:`)
  + (sourceOf(image) || image.src || image.name || '');
// The rendered row for an image, found by that key (indexes shift while ghosts play out).
export const rowElFor = (image) => {
  try { return listEl.querySelector(`li[data-key="${CSS.escape(rowKey(image))}"] .row`); }
  catch { return null; }
};

export const isPinned = (image) => state.showPinned && !!image.pinned;

// The page a row came from. Editor mode merges several pages into one list, so each row
// remembers its own; everywhere else that is just the scanned page.
export const rowResource = (image) => (image && image.resource) || state.activeUrl;

// A row that represents a project (a shared server-project row, or a local pin with kind
// 'project') — only these recolour their name; plain page images/pins keep the theme colour.
export const isProjectRow = (image) => !!image.shared || image.kind === 'project';

export const isOpened = (image) => state.markOpened && image.opened && image.opened.length > 0;

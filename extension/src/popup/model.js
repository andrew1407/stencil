import { sourceOf } from '../lib/imageModel.js';
import { listEl } from './panelDom.js';

// `mode` 'editor': the panel stands on the Stencil editor and the scan follows `sourceTabId`.
export const state = { all: [], filtered: [], mode: 'page', sourceTabId: null, editorTabId: null, activeTabId: null, activeUrl: '', markOpened: true, openedFirst: true, showPinned: true, hoverHighlight: false, connections: [], shared: [], openIn: { desktopScheme: 'stencil', telegramBotUsername: '' } };

// The tab the user is LOOKING at: anything that mounts UI on a page must target this one,
// or in editor mode it lands on a background tab.
export const surfaceTabId = () => (state.mode === 'editor' ? state.editorTabId : state.activeTabId);

// Row identity across renders. The source alone will not do: a server row and a local pin
// of the same image are different rows, as are a poster and a plain image sharing a URL.
export const rowKey = (image) => (image.shared
  ? `server:${image.serverUrl}:${image.projectId || ''}:`
  : `page:${image.kind || ''}:${image.poster ? 'p' : ''}${image.meta ? 'm' : ''}:`)
  + (sourceOf(image) || image.src || image.name || '');
// By key: indexes shift while ghosts play out.
export const rowElFor = (image) => {
  try { return listEl.querySelector(`li[data-key="${CSS.escape(rowKey(image))}"] .row`); }
  catch { return null; }
};

export const isPinned = (image) => state.showPinned && !!image.pinned;

// Editor mode merges several pages into one list, so each row remembers its own.
export const rowResource = (image) => (image && image.resource) || state.activeUrl;

export const isProjectRow = (image) => !!image.shared || image.kind === 'project';

export const isOpened = (image) => state.markOpened && image.opened && image.opened.length > 0;

// ── window.stencil's windows — the toolbar's modals, opened by title ─────────
// Each window is opened through its own shell, from its own toolbar control, so a
// scripted open flies out of the same button a click would.
import { closeOpenModal } from '../../ui/base.js';
import UI_STRINGS from '../../config/uiStrings.json' with { type: 'json' };
import { str } from '../coerce.js';

// The editor's windows, for stencil.openWindow(title). The table is config/uiStrings.json;
// an opener's disabled state gates the script route exactly as it gates the click.
export const WINDOWS = Object.freeze(UI_STRINGS.windows);
// Loose title matching: case-insensitive, punctuation/whitespace-free, so 'Visuals',
// 'visuals & settings', 'open-in' and 'Open In…' all land.
const windowNameKey = (v) => str(v).toLowerCase().replace(/[^a-z0-9]+/g, '');
const findWindow = (ref) => {
  const want = windowNameKey(ref);
  if (!want) return null;
  return WINDOWS.find((w) => [w.key, w.title, w.hotkey, ...(w.aliases || [])].some((n) => windowNameKey(n) === want)) || null;
};

export const createWindowsApi = () => {
  let stencil;   // the frozen facade, handed over by setFacade after the guard

  const api = {
    // Window titles come from config/uiStrings.json and match loosely (case/punctuation-free;
    // hotkey ids work too). A disabled control throws the button's reason.
    get windows() { return WINDOWS.map((w) => w.title); },
    openWindow(title) {
      const w = findWindow(title);
      if (!w) throw new Error(`stencil: no window called "${str(title)}" — one of: ${WINDOWS.map((x) => x.title).join(', ')}`);
      const doc = typeof document !== 'undefined' ? document : null;
      const ids = Array.isArray(w.opener) ? w.opener : [w.opener];
      const btn = doc && ids.map((id) => doc.getElementById(id)).find((el) => el && !el.hidden) || null;
      const shell = doc?.getElementById(w.overlay)?.__stencilModal;
      if (!btn || !shell) throw new Error(`stencil: the "${w.title}" window is not available here`);
      if (btn.disabled) throw new Error(`stencil: "${w.title}" is unavailable — ${btn.dataset?.disabledReason || 'its control is disabled'}`);
      if (!shell.isOpen()) shell.open(btn);
      return stencil;
    },
    // Closes whatever window is showing — the table's own shells first, then anything
    // else the shell registry knows (a confirm, the expiration prompt).
    closeWindow() {
      const doc = typeof document !== 'undefined' ? document : null;
      for (const w of WINDOWS) {
        const shell = doc?.getElementById(w.overlay)?.__stencilModal;
        if (shell?.isOpen()) shell.close();
      }
      closeOpenModal();
      return stencil;
    },
    // Which window is showing right now, by title (null when none).
    get openedWindow() {
      const doc = typeof document !== 'undefined' ? document : null;
      return WINDOWS.find((w) => doc?.getElementById(w.overlay)?.__stencilModal?.isOpen())?.title ?? null;
    },
    openProjectsWindow() { return stencil.openWindow('projects'); },
    openServersWindow() { return stencil.openWindow('servers'); },
    openConnectionsWindow() { return stencil.openWindow('servers'); },   // alias: the Servers window
    openLinksWindow() { return stencil.openWindow('links'); },
    openDescriptionWindow() { return stencil.openWindow('description'); },
    openKeywordsWindow() { return stencil.openWindow('keywords'); },
    openAssistantSettingsWindow() { return stencil.openWindow('assistant-settings'); },
    openShortcutsWindow() { return stencil.openWindow('shortcuts'); },
    openVisualsWindow() { return stencil.openWindow('visuals'); },
    openHelpWindow() { return stencil.openWindow('help'); },
    openImageWindow() { return stencil.openWindow('open-image'); },
    openCropWindow() { return stencil.openWindow('crop'); },
  };

  return { api, setFacade: (f) => { stencil = f; } };
};

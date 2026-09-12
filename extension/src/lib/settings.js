// ── Extension settings (chrome.storage.sync) ────────────────────────────────
// The one reader/writer of the Options page's settings, plus the `${origin}/*` match
// patterns the background scopes its registrations and permission requests to.

export const DEFAULT_EDITOR_URL = 'http://localhost:8080/';
export const DEFAULT_PAGE = 'A3';
// Default desktop-app URL scheme for the "Open in…" desktop hand-off (mirrors the browser
// app's openInConfig `desktopScheme`). Empty = hide the Desktop action.
const DEFAULT_DESKTOP_SCHEME = 'stencil';

// Settings live in chrome.storage.sync so they follow the user across machines.
export const getSettings = async () => {
  const s = await chrome.storage.sync.get({ editorUrl: DEFAULT_EDITOR_URL, page: DEFAULT_PAGE, markOpened: true, openedFirst: true, showPinned: true, hoverHighlight: false, highlightColor: 'theme', exposeWindowStencil: false, editorPageApi: true, desktopScheme: DEFAULT_DESKTOP_SCHEME, telegramBotUsername: '' });
  return {
    editorUrl: (s.editorUrl || DEFAULT_EDITOR_URL).trim() || DEFAULT_EDITOR_URL,
    page: s.page || DEFAULT_PAGE,
    // "Open in…" targets, mirroring the browser app's openInConfig. The desktop app's OS
    // URL scheme (default 'stencil'; empty hides the Desktop action) and the Telegram bot's
    // username (empty hides the Telegram action). Local operator config — see options.
    desktopScheme: typeof s.desktopScheme === 'string' ? s.desktopScheme.trim() : DEFAULT_DESKTOP_SCHEME,
    telegramBotUsername: typeof s.telegramBotUsername === 'string' ? s.telegramBotUsername.trim() : '',
    // The on-page highlight outline colour: 'theme' = follow the main accent, or a hex
    // string for a custom colour. Read live by the popup + page API when highlighting.
    highlightColor: typeof s.highlightColor === 'string' && s.highlightColor ? s.highlightColor : 'theme',
    // Whether the popup badges images that already have an editor (default on).
    markOpened: s.markOpened !== false,
    // Whether the popup sorts opened images to the top (default on). Toggled live from
    // the popup, persisted here. Independent of markOpened, but a no-op when badging is off.
    openedFirst: s.openedFirst !== false,
    // Whether the popup styles pinned images (gray outline) and floats them to the top
    // (default on). Toggled live from the popup; pinning still works when off.
    showPinned: s.showPinned !== false,
    // Whether hovering a list row outlines its element on the page — and, with the on-page
    // highlight on, hovering a page element outlines its row (default OFF). Side panel /
    // DevTools panel only; toggled live from the panel.
    hoverHighlight: s.hoverHighlight === true,
    // Whether to inject a page-global `window.stencil` scripting API into every page
    // (default OFF — touches every page's main world, so strictly opt-in).
    exposeWindowStencil: s.exposeWindowStencil === true,
    // Whether the EDITOR page gets `stencil.extension`. Default ON: unlike exposeWindowStencil
    // it touches exactly one origin — the configured editor, our own front-end.
    editorPageApi: s.editorPageApi !== false
  };
};

export const setSettings = async (patch) => {
  await chrome.storage.sync.set(patch);
};

// `${origin}/*` match pattern for a URL, or null when it isn't an http(s) origin we
// can scope tabs.query / a content script / a permission request to.
export const originPattern = (url) => {
  try {
    const origin = new URL(url).origin;
    return origin.startsWith('http') ? `${origin}/*` : null;
  } catch {
    return null;
  }
};

// originPattern for the configured editor. Shared by the background bridge
// registration and resumeInOpenEditor.
export const editorOriginPattern = async () => {
  try {
    return originPattern((await getSettings()).editorUrl);
  } catch {
    return null;
  }
};


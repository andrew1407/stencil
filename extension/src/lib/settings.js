// The Options page's settings (chrome.storage.sync, so they follow the user), plus the
// `${origin}/*` patterns the background scopes registrations and permission requests to.

export const DEFAULT_EDITOR_URL = 'http://localhost:8080/';
export const DEFAULT_PAGE = 'A3';
// Mirrors the browser app's openInConfig `desktopScheme`; empty hides the Desktop action.
const DEFAULT_DESKTOP_SCHEME = 'stencil';

export const getSettings = async () => {
  const s = await chrome.storage.sync.get({ editorUrl: DEFAULT_EDITOR_URL, page: DEFAULT_PAGE, markOpened: true, openedFirst: true, showPinned: true, hoverHighlight: false, highlightColor: 'theme', exposeWindowStencil: false, editorPageApi: true, desktopScheme: DEFAULT_DESKTOP_SCHEME, telegramBotUsername: '' });
  return {
    editorUrl: (s.editorUrl || DEFAULT_EDITOR_URL).trim() || DEFAULT_EDITOR_URL,
    page: s.page || DEFAULT_PAGE,
    // "Open in…" targets; an empty Telegram username hides the Telegram action.
    desktopScheme: typeof s.desktopScheme === 'string' ? s.desktopScheme.trim() : DEFAULT_DESKTOP_SCHEME,
    telegramBotUsername: typeof s.telegramBotUsername === 'string' ? s.telegramBotUsername.trim() : '',
    // 'theme' = follow the accent, else a hex string.
    highlightColor: typeof s.highlightColor === 'string' && s.highlightColor ? s.highlightColor : 'theme',
    markOpened: s.markOpened !== false,
    openedFirst: s.openedFirst !== false,
    showPinned: s.showPinned !== false,
    hoverHighlight: s.hoverHighlight === true,
    // Default OFF: it touches every page's main world, so strictly opt-in.
    exposeWindowStencil: s.exposeWindowStencil === true,
    // Default ON: it touches exactly one origin — the configured editor, our own front-end.
    editorPageApi: s.editorPageApi !== false
  };
};

export const setSettings = async (patch) => {
  await chrome.storage.sync.set(patch);
};

// null when the URL is not an http(s) origin a content script can be scoped to.
export const originPattern = (url) => {
  try {
    const origin = new URL(url).origin;
    return origin.startsWith('http') ? `${origin}/*` : null;
  } catch {
    return null;
  }
};

export const editorOriginPattern = async () => {
  try {
    return originPattern((await getSettings()).editorUrl);
  } catch {
    return null;
  }
};


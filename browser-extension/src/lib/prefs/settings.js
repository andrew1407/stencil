// The Options page's settings (chrome.storage.sync, so they follow the user), plus the
// `${origin}/*` patterns the background scopes registrations and permission requests to.

export const DEFAULT_EDITOR_URL = 'http://localhost:8080/';
// Where a copied "Open in Stencil" link points: the published app, unless the user names another.
export const DEFAULT_SHARE_URL = 'https://andrew1407.github.io/stencil/';
const httpUrl = (v) => { try { return /^https?:$/.test(new URL(v).protocol); } catch { return false; } };
export const DEFAULT_PAGE = 'A3';
// Mirrors the browser app's openInConfig `desktopScheme`; empty hides the Desktop action.
const DEFAULT_DESKTOP_SCHEME = 'stencil';

export const getSettings = async () => {
  const s = await chrome.storage.sync.get({ editorUrl: '', page: DEFAULT_PAGE, markOpened: true, openedFirst: true, showPinned: true, hoverHighlight: false, highlightColor: 'theme', exposeWindowStencil: false, editorPageApi: true, desktopScheme: DEFAULT_DESKTOP_SCHEME, telegramBotUsername: '', shareUrl: '' });
  const typedShare = typeof s.shareUrl === 'string' ? s.shareUrl.trim() : '';
  const typedUrl = typeof s.editorUrl === 'string' ? s.editorUrl.trim() : '';
// A stored default is no choice: older Options pages saved it on any Save.
  const chosenUrl = typedUrl === DEFAULT_EDITOR_URL ? '' : typedUrl;
  const editorPageApiToggle = s.editorPageApi !== false;
  return {
    editorUrl: typedUrl || DEFAULT_EDITOR_URL,
    editorUrlSet: chosenUrl !== '',
    page: s.page || DEFAULT_PAGE,
    // An http(s) address the user typed, else the published app.
    shareUrl: httpUrl(typedShare) ? typedShare : DEFAULT_SHARE_URL,
    shareUrlSet: httpUrl(typedShare),
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
    // OFF until the user sets editorUrl: the privileged relay trusts only an origin they chose.
    editorPageApi: editorPageApiToggle && chosenUrl !== '',
    editorPageApiToggle
  };
};

export const setSettings = async (patch) => {
  await chrome.storage.sync.set(patch);
};

// True when the editor sits under a path of its origin, so every other page there shares the origin
// the privileged editor relay trusts (a github.io user site, a host's /stencil/ folder).
export const isSharedOrigin = (url) => {
  try {
    const u = new URL(String(url || ''));
    return /^https?:$/.test(u.protocol) && !['', '/', '/index.html'].includes(u.pathname);
  } catch {
    return false;
  }
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


// The `stencil://` link the desktop app opens (desktop/src/app/open/launchOptions.cpp parses it):
// the picture inline as `src`, then the script and its mode. Grammar and order are
// browser/js/core/launch/deepLink.js buildStencilSchemeUrl's; the tests pin the pair.

// The scheme the desktop registers; fixed, so a workspace cannot send a script elsewhere.
const DESKTOP_SCHEME = 'stencil';

// The OS launch machinery (LaunchServices, xdg-open argv) tolerates far less than a page URL:
// browser/js/core/launch/desktopLink.js INLINE_MAX_CHARS, pinned by the tests.
const MAX_DESKTOP_LINK = 1_000_000;

const buildDesktopUrl = ({ src, layout, incognito, script, scriptMode } = {}) => {
  const params = [];
  const add = (key, value) => params.push(`${key}=${encodeURIComponent(value)}`);
  if (src) {
    add('src', src);
    if (layout) add('layout', typeof layout === 'string' ? layout : JSON.stringify(layout));
  }
  if (incognito) add('incognito', '1');
  if (script) {
    add('script', script);
    add('scriptMode', scriptMode === 'open' ? 'open' : 'run');
  }
  return `${DESKTOP_SCHEME}://open?${params.join('&')}`;
};

// A picked picture rides as a data URL, a named one as its http(s) URL.
const desktopLaunch = (launch, { mode = 'run', incognito = false } = {}) => buildDesktopUrl({
  src: launch?.dataUrl || launch?.src || '',
  layout: launch?.layout,
  incognito,
  script: launch?.script ?? '',
  scriptMode: mode,
});

const isTooBigForDesktop = (url) => String(url ?? '').length > MAX_DESKTOP_LINK;

export { DESKTOP_SCHEME, MAX_DESKTOP_LINK, buildDesktopUrl, desktopLaunch, isTooBigForDesktop };

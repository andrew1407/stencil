// The stencil:// link a desktop hand-off opens: a server reference (no bytes, no token), else
// the picture inline, plus an optional .stc and its mode. Shared by "Open in another app" and
// the Script window's Desktop app menu; the grammar is buildStencilSchemeUrl's.
import { buildStencilSchemeUrl } from './deepLink.js';

// Inline hand-offs ride the OS launch machinery (LaunchServices / xdg-open argv), which
// tolerates far less than an in-page URL.
export const INLINE_WARN_CHARS = 200_000;
export const INLINE_MAX_CHARS = 1_000_000;

// `payload` is openInLaunchPayload's shape, or null for a script with no picture.
export const desktopLaunchUrl = ({ scheme, payload = null, incognito = false, script = '', scriptMode = 'run' } = {}) => {
  const common = { scheme, incognito: incognito || !!payload?.incognito, script, scriptMode };
  if (payload?.server) {
    const { url, id, version } = payload.server;
    return buildStencilSchemeUrl({ ...common, server: url, id, version });
  }
  return buildStencilSchemeUrl({ ...common, src: payload?.dataUrl || undefined, layout: payload?.dataUrl ? payload.layout : undefined });
};

// 'refuse' | 'warn' | 'ok': only a link carrying bytes or a script inline is measured.
export const inlineVerdict = (url, { payload = null, script = '' } = {}) => {
  if (payload?.server && !script) return 'ok';
  if (url.length > INLINE_MAX_CHARS) return 'refuse';
  return url.length > INLINE_WARN_CHARS ? 'warn' : 'ok';
};

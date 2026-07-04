// ── "Open in…" operator config loader ───────────────────────────
// The toolbar's Open-in modal targets (the desktop URL scheme + the optional Telegram
// bot username) are operator config, kept in a LOCAL, gitignored openInConfig.json —
// the static-site equivalent of a .env file (copy openInConfig.example.json and fill
// it in). Loaded at runtime via fetch, NOT a static JSON import, so a fresh clone with
// no local file still boots — it just falls back to these defaults (desktop enabled,
// Telegram hidden until a bot username is set). The promise is cached, so the config is
// fetched at most once and shared by every caller (DrawingApp + the modal).

export const OPEN_IN_DEFAULTS = { desktopScheme: 'stencil', telegramBotUsername: '' };

let cached = null;
export const loadOpenInConfig = () => {
  if (cached) return cached;
  cached = (async () => {
    try {
      const res = await fetch(new URL('./openInConfig.json', import.meta.url));
      if (!res.ok) return { ...OPEN_IN_DEFAULTS };
      const raw = await res.json();
      return {
        desktopScheme: typeof raw.desktopScheme === 'string' ? raw.desktopScheme : OPEN_IN_DEFAULTS.desktopScheme,
        telegramBotUsername: typeof raw.telegramBotUsername === 'string' ? raw.telegramBotUsername : '',
      };
    } catch {
      return { ...OPEN_IN_DEFAULTS };
    }
  })();
  return cached;
};

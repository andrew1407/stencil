// Operator config (the desktop URL scheme, the optional Telegram bot username) in a
// LOCAL, gitignored JSON file beside this module — the static-site .env. Fetched at
// runtime, not imported, so a fresh clone boots on the defaults; the promise is cached.

export const OPEN_IN_DEFAULTS = Object.freeze({ desktopScheme: 'stencil', telegramBotUsername: '' });

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

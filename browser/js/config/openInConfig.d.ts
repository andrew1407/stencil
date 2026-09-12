export interface OpenInConfig {
  desktopScheme: string;
  telegramBotUsername: string;
}

export declare const OPEN_IN_DEFAULTS: OpenInConfig;

/** Fetches the local, gitignored openInConfig.json (defaults on a fresh clone); cached. */
export declare const loadOpenInConfig: () => Promise<OpenInConfig>;

export interface Settings {
  editorUrl: string;
  page: string;
  desktopScheme: string;
  telegramBotUsername: string;
  highlightColor: string;
  markOpened: boolean;
  openedFirst: boolean;
  showPinned: boolean;
  hoverHighlight: boolean;
  exposeWindowStencil: boolean;
  editorPageApi: boolean;
}

export declare const DEFAULT_EDITOR_URL: string;
export declare const DEFAULT_PAGE: string;
export declare function getSettings(): Promise<Settings>;
export declare function setSettings(patch: Partial<Settings>): Promise<void>;
/** null when `url` is not an http(s) origin a content script can be scoped to. */
export declare function originPattern(url: string): string | null;
export declare function editorOriginPattern(): Promise<string | null>;

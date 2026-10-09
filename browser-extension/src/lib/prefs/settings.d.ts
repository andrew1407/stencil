export interface Settings {
  editorUrl: string;
  /** True once the user saved an editor URL of their own (the default does not count). */
  editorUrlSet: boolean;
  page: string;
  desktopScheme: string;
  telegramBotUsername: string;
  highlightColor: string;
  markOpened: boolean;
  openedFirst: boolean;
  showPinned: boolean;
  hoverHighlight: boolean;
  exposeWindowStencil: boolean;
  /** The effective gate: the toggle AND a user-set editor URL. */
  editorPageApi: boolean;
  /** The Options checkbox as saved (default on). */
  editorPageApiToggle: boolean;
}

export declare const DEFAULT_EDITOR_URL: string;
export declare const DEFAULT_PAGE: string;
export declare function getSettings(): Promise<Settings>;
export declare function setSettings(patch: Partial<Settings>): Promise<void>;
/** null when `url` is not an http(s) origin a content script can be scoped to. */
export declare function originPattern(url: string): string | null;
export declare function editorOriginPattern(): Promise<string | null>;

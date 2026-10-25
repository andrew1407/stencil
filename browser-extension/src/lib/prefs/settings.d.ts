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
  /** Where a copied "Open in Stencil" link points: the user's http(s) address, else DEFAULT_SHARE_URL. */
  shareUrl: string;
  /** True once the user saved a share address of their own. */
  shareUrlSet: boolean;
}

export declare const DEFAULT_EDITOR_URL: string;
export declare const DEFAULT_SHARE_URL: string;
export declare const DEFAULT_PAGE: string;
export declare function getSettings(): Promise<Settings>;
export declare function setSettings(patch: Partial<Settings>): Promise<void>;
/** null when `url` is not an http(s) origin a content script can be scoped to. */
export declare function originPattern(url: string): string | null;
export declare function editorOriginPattern(): Promise<string | null>;
/** True when the editor sits under a path of its origin, which every other page there shares. */
export declare const isSharedOrigin: (url: string) => boolean;

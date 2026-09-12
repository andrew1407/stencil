// Context-menu click resolution over contextMenuItems.js's ids, plus its re-exports.
export declare const MENU: Record<string, string>;
export interface MenuItem { id: string; parentId?: string; title: string; contexts: string[]; visible?: boolean; }
export declare const MENU_ITEMS: MenuItem[];
export declare const DYNAMIC_ITEMS: string[];
export declare const PIN_ITEMS: string[];
export declare const PREVIEW_ITEMS: string[];
export declare const STATIC_DESKTOP_ITEMS: string[];
export declare const pinItemTitle: (pinned: boolean, kind?: 'image' | 'video') => string;

export declare const menuVisibilityFor: (data: { url?: string; video?: boolean; poster?: string } | null)
  => { bg: boolean; preview: boolean };
export declare const visibleMenu: (context: string, revealed?: string[]) => string[];

export interface ContextAction {
  action: string; src: string; incognito?: boolean; open?: string; target?: string;
}
export declare const resolveContextAction: (info?: { menuItemId?: string; srcUrl?: string },
  recordedUrl?: string | null) => ContextAction | null;

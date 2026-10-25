export declare const MENU: Record<string, string>;
export interface MenuItem {
  id: string; parentId?: string; title: string; contexts: string[]; visible?: boolean;
}
export declare const MENU_ITEMS: MenuItem[];
export declare const DYNAMIC_ITEMS: string[];
export declare const STATIC_DESKTOP_ITEMS: string[];
export declare const PIN_ITEMS: string[];
export declare const pinItemTitle: (pinned: boolean, kind?: 'image' | 'video') => string;
export declare const PREVIEW_ITEMS: string[];
/** The "Copy Stencil link" items, on images, videos and backgrounds. */
export declare const LINK_ITEMS: string[];
/** The ones among LINK_ITEMS whose link carries `incognito`. */
export declare const INCOGNITO_LINK_ITEMS: string[];

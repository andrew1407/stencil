// A project's "Copy ›" entry for the row menus.
import type { ShareLinkKind } from '../../core/launch/shareLinks.js';

export interface MenuItem {
  icon: string; label: string; danger?: boolean;
  onClick?: (at?: DOMRect) => unknown;
  /** A second click inside the double-click window runs this instead of onClick. */
  onDoubleClick?: (at?: DOMRect) => unknown;
  items?: MenuItem[];
}
export interface ProjectFacts { name: string; source: string; serverUrl: string; remoteId: string }

export declare const LINK_LABELS: Readonly<Record<ShareLinkKind, string>>;
export declare const LINK_ICONS: Readonly<Record<ShareLinkKind, string>>;
export declare const IMAGE_LABELS: Readonly<Record<'current' | 'tint' | 'original', string>>;
export declare const IMAGE_ICONS: Readonly<Record<'current' | 'tint' | 'original', string>>;
/** The open editor's name, source and server link. */
export declare const liveFacts: (app: object) => ProjectFacts;
/** The name copied on a click; with `shares`, its link shared on a double click. Null without a name. */
export declare const nameItem: (facts: Partial<ProjectFacts>, shares?: boolean) => MenuItem | null;
/** The links a project has: copied on a click and shared on a double click, or the other way round with `shareFirst`. */
export declare const linkItems: (facts: Partial<ProjectFacts>, shareFirst?: boolean) => MenuItem[];
/** The share button's menu: Image › Current / No lines / Original, then each link; click shares, double click copies. */
export declare const shareMenuItems: (app: object) => MenuItem[];
/** "Copy entries ›" and "Copy image ›", siblings; `meta` null = the open editor, `remote` a server-only listing row. */
export declare const copyMenuItems: (app: object, opts?: { meta?: object | null; remote?: boolean }) => MenuItem[];

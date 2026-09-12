import type { DrawingApp } from '../core/drawingApp.js';

export interface LogoAccentMenu {
  menuShowing(): boolean;
  closeMenu(): void;
  altPeek(): void;
  /** 'peek' | 'sticky' | null — which gesture opened the menu, if any. */
  menuKind(): string | null;
}

/** The logo's right-click / Alt-hover accent preset menu. */
export declare function wireLogoAccentMenu(logo: HTMLElement, wrap: HTMLElement, app: DrawingApp): LogoAccentMenu;

// Shapes for popup/menu.js — the shared lib/actionMenu.js controller behind row,
// editor-list and the logo's drag menus, plus this panel's own open/close wiring.
import type { PopupImage } from '../list/model.js';

export interface ActionMenu {
  item(glyph: string, label: string, fn: () => unknown): HTMLElement;
  sep(): HTMLElement;
  label(text: string): HTMLElement;
  submenu(iconHtml: string, labelText: string, children: HTMLElement[]): HTMLElement;
  place(x: number, y: number, origin?: { x: number; y: number }): void;
  openAnchored(btn: HTMLElement, fill: () => void): void;
  openNodes(btn: HTMLElement, nodes: HTMLElement[]): void;
  openAt(x: number, y: number, fill: () => void): void;
  close(): void;
  anchor(): HTMLElement | null;
}

/** `item`/`sep`/`label`/`submenu` also live on this object — destructured at the call
 * site, so they carry no export the module graph can see. */
export declare const menu: ActionMenu;
export declare const closeMenu: ActionMenu['close'];
export declare const placeMenu: ActionMenu['place'];
export declare const openMenu: (btn: HTMLElement, image: PopupImage) => void;
export declare const openMenuNodes: ActionMenu['openNodes'];
export declare const openMenuAt: (image: PopupImage, x: number, y: number) => void;

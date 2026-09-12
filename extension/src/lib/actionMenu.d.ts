export interface MenuPlacement { left: number; top: number; }
export declare const menuPlacement: (args: { x: number; y: number; size: { width: number; height: number };
  viewport: { width: number; height: number } }) => MenuPlacement;
export declare const anchoredX: (args: { rect: { left: number; right: number }; width: number }) => number;
export declare const flyoutPlacement: (args: { head: { left: number; right: number; top: number };
  wrap: { left: number; top: number }; size: { width: number; height: number };
  viewport: { width: number; height: number } }) => { left: number; top: number };

export interface ActionMenu {
  item(glyph: string, label: string, fn: () => unknown): HTMLButtonElement;
  sep(): HTMLDivElement;
  label(text: string): HTMLDivElement;
  submenu(iconHtml: string, labelText: string, children: HTMLElement[]): HTMLDivElement;
  place(x: number, y: number, origin?: { x: number; y: number }): void;
  openAnchored(btn: HTMLElement, fill: () => void): void;
  openNodes(btn: HTMLElement, nodes: HTMLElement[]): void;
  openAt(x: number, y: number, fill: () => void): void;
  close(): void;
  anchor(): HTMLElement | null;
}
export declare const createActionMenu: (opts: { menuEl: HTMLElement; run?: (fn: () => unknown) => unknown;
  doc?: Document; win?: Window }) => ActionMenu;

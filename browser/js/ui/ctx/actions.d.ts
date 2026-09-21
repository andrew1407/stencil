/** Wire the context menu's image, layout, clipboard and draw rows. */
export declare const wireCtxActions: (app: object, deps: {
  menu: HTMLElement; closeMenu: () => void;
  wireSubmenu: (parent: HTMLElement, sub: HTMLElement) => void;
}) => void;

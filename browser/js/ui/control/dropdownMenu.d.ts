import type { Point } from '../../core/geometry.js';
export declare const menuDustPoint: (trigger: HTMLElement | null | undefined) => Point | null;
export declare const placeMenu: (menu: HTMLElement | null, trigger: HTMLElement | null) => void;
export declare const showMenu: (menu: HTMLElement | null, trigger: HTMLElement) => void;
export declare const hideMenu: (menu: HTMLElement | null) => void;

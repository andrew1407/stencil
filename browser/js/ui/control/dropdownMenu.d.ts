import type { Point } from '../../core/geometry.js';
export declare const menuDustPoint: (trigger: HTMLElement | null | undefined) => Point | null;
/** Points the slide entrance's transform-origin at `point` (the caret the motes fly from). */
export declare const growFrom: (menu: HTMLElement | null, point: Point | null,
                                opts?: { left?: number | null; above?: boolean }) => void;
export declare const placeMenu: (menu: HTMLElement | null, trigger: HTMLElement | null) => void;
export declare const showMenu: (menu: HTMLElement | null, trigger: HTMLElement) => void;
export declare const hideMenu: (menu: HTMLElement | null) => void;

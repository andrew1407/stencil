import type { Renderer } from '../draw/renderer.js';
/** The colour a line's points draw in: its own pointColor when set, else its stroke colour. */
export declare const pointColorOf: (line: { color: string; pointColor?: string }) => string;
export declare function drawLine(r: Renderer, line: object, isSelected?: boolean, lineIdx?: number): void;
export declare function drawPoint(r: Renderer, point: { x: number; y: number }, color: string,
                                 pointSize?: number, isSelected?: boolean, highlightState?: 0 | 1 | 2): void;

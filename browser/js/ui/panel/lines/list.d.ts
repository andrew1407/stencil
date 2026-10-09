import type { DrawingApp } from '../../../core/drawingApp.js';

/** Toggle #lines-list rows' hover class to match app.hoverLineIdx. */
export declare const applyLinesListHover: (app: DrawingApp) => void;

/** Rebuild the Lines tab's rows from app.lines: number, name, line colour, thickness, point colour, point size, points, eye, bin; a hidden line's row is dimmed. No-op while the tab is hidden. */
export declare const renderLinesList: (app: DrawingApp) => void;

// ~ pressed twice toggles drawing. Desktop twin: app/events/MainWindowEvents.cpp keyPressEvent.
import type { DrawingApp } from '../../../core/drawingApp.js';

/** A press counter: true when the press at `now` (ms) completes a double-press. */
export declare const createDoublePress: (windowMs?: number) => (now: number) => boolean;
export declare function wireDrawDoublePress(app: DrawingApp, doc?: Document): void;

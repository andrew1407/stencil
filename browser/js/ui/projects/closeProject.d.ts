import type { DrawingApp } from '../../core/drawingApp.js';

/** The question: names the project and says it stays saved in Projects. */
export declare const closeProjectMessage: (name: string | null | undefined) => string;

/** Asks (unless `ask: false`), then closes the project open here; a toast when none is open. True once closed. */
export declare function confirmCloseProject(app: DrawingApp,
  opts?: { closeAnchor?: Element | DOMRect | null; ask?: boolean }): Promise<boolean>;

import type { DrawingApp } from '../../core/drawingApp.js';
import type { OpenImageSource, OpenImageTabs } from './tabs.js';

/** Open here / in a new tab / over the current project — each resolves the source first. */
export declare function wireOpenActions(args: {
  app: DrawingApp;
  els: {
    hereBtn: HTMLButtonElement;
    newTabBtn: HTMLButtonElement;
    replaceBtn: HTMLButtonElement;
    incog: HTMLInputElement;
    renameEl: HTMLInputElement;
    keepEl: HTMLInputElement;
  };
  preview: OpenImageTabs;
  src: OpenImageSource;
  frameSeconds: () => number;
  openOpts: () => Record<string, unknown>;
  target: () => string | null;
  canReplace: () => boolean;
  close: () => void;
}): void;

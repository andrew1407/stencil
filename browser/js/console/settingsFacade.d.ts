import type { DrawingApp } from '../core/drawingApp.js';
import type { StencilSettings } from './stencilApi.js';

export interface SettingsFacadeDeps {
  app: DrawingApp;
  guard: <T extends object>(obj: T) => T;
}

export interface SettingsFacade {
  /** A fresh accessor object per call; also spread onto the top-level facade. */
  settingsAccessors(): StencilSettings;
  /** The guarded accessor object — window.stencil.settings. */
  settings(): StencilSettings;
}

export declare const createSettingsFacade: (deps: SettingsFacadeDeps) => SettingsFacade;

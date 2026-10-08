import type { DrawingApp } from '../../../core/drawingApp.js';
import type { DockMode } from '../dock.js';
import type { PanelTurn } from './turn.js';

/** `app.chat`: the chat panel's scripting surface, riding the panel's own code paths. */
export interface PanelApi {
  open(): void;
  close(): void;
  isOpen(): boolean;
  dock(mode: DockMode | string): void;
  /**
   * Opens docked on a side, or floating with its top-left corner on a client point; an open panel
   * moves there. A float forms out of `from` (a client rect), else out of the toolbar icon.
   */
  openAt(spot: 'left' | 'right' | 'top' | 'bottom' | { x: number; y: number },
         opts?: { from?: { left: number; top: number; width: number; height: number } | null }): void;
  prompt(text: string, images?: string[]): Promise<unknown>;
  history(): { role: string; text: string }[];
  /** True when a turn was actually running. */
  abort(): boolean;
  clear(): void;
  readonly isSending: boolean;
  controller: () => unknown;
  readonly voiceInput: boolean;
  setVoiceInput(on: boolean): void;
}

export declare function createPanelApi(deps: {
  app: DrawingApp;
  ctrl: () => unknown;
  turn: PanelTurn;
  setOpen: (on: boolean) => void;
  panelIsOpen: () => boolean;
  adoptLayout: () => void;
  setDock: (mode: DockMode) => void;
  floatAt: (x: number, y: number) => void;
  openFrom: (from: { left: number; top: number; width: number; height: number } | null, run: () => void) => void;
  renderAttachments: () => void;
  updateControls: () => void;
  voice: () => { isOn(): boolean; setMode(on: boolean): void } | null;
}): PanelApi;

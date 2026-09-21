import type { StencilElement } from '../base.js';

/** A dropped or uploaded .stc: into the open window's editor, else straight onto the project. */
export declare const loadScriptFile: (file: File) => Promise<void>;
export declare const isScriptModalOpen: () => boolean;

/** The script window: a .stc editor over the shared paint pass (editor.js). */
export declare class StencilScriptModal extends StencilElement {
  static inner(): string;
  static template(): string;
  wire(app: object): void;
}

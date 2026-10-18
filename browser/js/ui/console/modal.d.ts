// The console window: the CLI's command line over the open project, blocking nothing.
import type { StencilElement } from '../base.js';
export declare class StencilConsoleModal extends StencilElement {
  static inner(): string;
  static template(): string;
  wire(app: object): void;
}

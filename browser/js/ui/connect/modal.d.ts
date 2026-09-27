import type { StencilElement } from '../base.js';

export { matchesConnFilter, batchNote } from './rules.js';

/** The server connections modal, backed by app.connections (net/connectionManager.js). */
export declare class StencilConnectModal extends StencilElement {
  static inner(): string;
  static template(): string;
  wire(app: object): void;
}

import type { StencilElement } from './base.js';

/** 'all' | 'admin' | 'non-admin' — admin means a credential that can mint session tokens. */
export declare const matchesConnFilter: (conn: { credentialKind?: string } | null | undefined, mode: string) => boolean;

/** One server is named outright, several are counted (desktop parity). */
export declare const batchNote: (verb: string, urls: readonly string[]) => string;

/** The server connections modal, backed by app.connections (net/connectionManager.js). */
export declare class StencilConnectModal extends StencilElement {
  static inner(): string;
  static template(): string;
  wire(app: object): void;
}

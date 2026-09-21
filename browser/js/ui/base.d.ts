// Web Component base for the light-DOM custom elements: each UI region owns its markup
// (static inner()) and behaviour (wire(app)), wired after the one-shot `stencil:ready`.
// Off-browser the base is a plain class and define() a no-op, so Node can import markup.
import type { DrawingApp } from '../core/drawingApp.js';
import type { ConnectionManager } from '../net/connectionManager.js';

export { escapeHtml } from './escapeHtml.js';
export { closeOpenModal } from './modalRegistry.js';
export { MODAL_CLOSE_MS, createModalFlight } from './modalFlight.js';
export { wireModalShell } from './modalShell.js';

/** Register a custom element — only in a browser. */
export declare const define: (tag: string, klass: CustomElementConstructor) => void;

export declare class StencilElement extends HTMLElement {
  /** Subclasses provide the region's markup; rendered once on connect when the host is empty. */
  static inner?: () => string;
  connectedCallback(): void;
  /** This element's own subtree, by id — never another region's nodes. */
  $<T extends HTMLElement = HTMLElement>(id: string): T | null;
  /** A bubbling CustomEvent: how a child tells the region it belongs to what happened. */
  emit<D = unknown>(type: string, detail?: D): boolean;
  /** Overridden by subclasses that need behaviour, in `stencil:ready` registration order. */
  wire(app: DrawingApp): void;
}

/** `<tag attrs>inner</tag>` for layout(). */
export declare const hostTag: (tag: string, attrs: string | null | undefined, inner: string) => string;
export declare const attachSearchFilter: (searchInput: HTMLElement, applyFilterFn: (e: Event) => void) => void;
/** Empty query matches everything; otherwise a case-insensitive substring match. */
export declare const rowMatches: (text: unknown, query: unknown) => boolean;
/** "Local" plus one option per connected server; false (row hidden) when there are none. */
export declare const fillTargetSelect: (
  selectEl: HTMLSelectElement, rowEl: HTMLElement | null, connMgr: ConnectionManager | null | undefined, allow?: boolean,
) => boolean;

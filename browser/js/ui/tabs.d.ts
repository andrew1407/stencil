// Shape of the shared tab strip (tabs.js): the element, and the detail its one event carries.
import { StencilElement } from './base.js';

/** What `tab-change` reports: the tab now showing, and the one that was. */
export interface TabChangeDetail {
  tab: string;
  previous: string;
}

export declare class StencilTabs extends StencilElement {
  /** The class the strip toggles, from the `active-class` attribute; `is-active` by default. */
  readonly activeClass: string;
  /** `data-tab` of the tab now showing. */
  readonly active: string;
  /**
   * Show `name`: marks its tab, shows its `[data-panel]`, hides the rest, then emits a
   * bubbling `tab-change`. Emits even when `name` is already active.
   */
  select(name: string): void;
}

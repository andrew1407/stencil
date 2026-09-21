/** One row of the plan: a stable key, the item it came from, and how to build the element. */
export interface PlanEntry {
  key: string;
  item?: PlanItem;
  build: () => HTMLElement;
}

/** One sortable project, local or remote, flattened for the comparators. */
export interface PlanItem {
  key: string;
  name: string;
  date: number;
  isRemote: boolean;
  meta: Record<string, unknown>;
  build: () => HTMLElement;
}

export interface RowPlan {
  buildItems(opts: { applySearch: boolean }): PlanItem[];
  sortItems(items: readonly PlanItem[], mode: string): PlanItem[];
  /** Every row the current state would list, in order — synthetic rows first. */
  rowPlan(): PlanEntry[];
  showsServer(): boolean;
}

export declare function createRowPlan(ctx: Record<string, unknown>): RowPlan;

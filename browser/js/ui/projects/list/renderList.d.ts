export interface RenderList {
  /** Rebuild the list from the plan, then re-gate Clear All and the batch bar. */
  render(): void;
  /** Take the hold with the list's height; await the result after the rows have flown. */
  beginRemoval(): () => Promise<void>;
  removing(): boolean;
  /** The keys the last render listed — one array, mutated in place each render. */
  shownKeys: string[];
  rowByFilterKey(key: string): HTMLElement | null;
}

export declare function createRenderList(ctx: Record<string, unknown>): RenderList;

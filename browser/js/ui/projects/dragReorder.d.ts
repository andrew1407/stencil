/** The drag surface the modal keeps: row wiring, the live key→item map, and the render gate. */
export interface DragReorder {
  attachRowDrag(row: HTMLElement, key: string): void;
  /** Row key → the rendered item, so a drop on a zone resolves without re-parsing the key. */
  keyMeta: Map<string, { meta: Record<string, unknown>; isRemote: boolean }>;
  /** True while a row is held; a rebuild would destroy it, so renders hold off. */
  isDragging(): boolean;
}

export function createDragReorder(deps: Record<string, unknown>): DragReorder;

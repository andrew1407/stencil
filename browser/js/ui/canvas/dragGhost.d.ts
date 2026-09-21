export interface DragGhost {
  el: HTMLElement;
  move(x: number, y: number): void;
  destroy(): void;
}

/** A cloned, floating copy of `row` that tracks the pointer; `grabX/grabY` is where it was picked up. */
export declare function createDragGhost(row: HTMLElement, grabX: number, grabY: number, opacity?: number): DragGhost;

/** Suppresses the native HTML5 drag image and runs a DragGhost instead; returns an early teardown. */
export declare function setTranslucentDragImage(e: DragEvent, row: HTMLElement, opacity?: number): () => void;

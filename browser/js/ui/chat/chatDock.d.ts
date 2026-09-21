/** A floating panel's rect in viewport pixels (ui/chatGeometry.js clamps it). */
export interface FloatRect { x: number; y: number; w: number; h: number; }

/** Where the panel lives. Session-only: every page load starts closed and docked left. */
export type DockMode = 'left' | 'right' | 'top' | 'bottom' | 'float';

export interface ChatDock {
  mode(): DockMode;
  /** Move the panel; re-forms its dust out of the NEW edge and announces the layout. */
  setDock(mode: DockMode): void;
  /** Arm the header drag, the edge resizers and the float handles. Call once, after wire(). */
  wireGestures(): void;
  /** Re-measure the editor-column insets and publish chatLayoutChanged. */
  announceLayout(): void;
  /** Mark the current layout as the user's choice, ending any transient popover shape. */
  adoptLayout(): void;
  /** Put back the layout a compact popover displaced. A no-op when none is showing. */
  restoreFromCompact(): void;
  rect(): FloatRect;
  clampRect(r: FloatRect): FloatRect;
  /** True while the panel is wearing the transient compact-popover shape. */
  isCompact(): boolean;
  enterCompact(r: FloatRect): void;
}

export function createChatDock(deps: Record<string, unknown>): ChatDock;

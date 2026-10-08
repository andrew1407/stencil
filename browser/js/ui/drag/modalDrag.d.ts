import type { IconDragHooks } from './iconDrag.js';
import type { ModalShellApi } from '../modal/registry.js';

/** One row per window opener in config/uiStrings.json `windows`: [opener id, overlay id]. */
export declare function windowOpeners(
  windows?: readonly { opener: string | readonly string[]; overlay: string }[],
): [string, string][];

/** The drag hooks for one opener: refused until its shell is wired; a drop opens it at the point. */
export declare function modalDragHooks(
  btn: HTMLElement,
  overlayId: string,
  shells?: (overlayId: string) => ModalShellApi | null,
): Pick<Required<IconDragHooks>, 'start' | 'drop'>;

/** Wires every window opener under `root` (looked up through its scoped `$`); the ids wired. */
export declare function wireModalDrags(root: { $(id: string): HTMLElement | null } | null): string[];

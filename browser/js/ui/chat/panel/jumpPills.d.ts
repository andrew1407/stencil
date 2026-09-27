export interface JumpPills {
  /** Drop the cached pill boxes; re-measured on the next hover sync. */
  invalidatePillRects: () => void;
}

/** The ⌃/⌄ jump pills over the transcript, and the hovered row's "…" that yields to them. */
export declare function wireJumpPills(deps: {
  transcript: HTMLElement;
  jumps: HTMLElement;
  jumpPills: HTMLElement[];
}): JumpPills;

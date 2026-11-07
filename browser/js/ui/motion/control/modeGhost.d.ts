/** Sliding's group flight: `el`'s picture slides whole in or out along its axis inside `r`, `el` hidden under it; false when nothing plays. */
export declare function slideGhost(el: HTMLElement | null | undefined, r: { left: number; top: number; width: number; height: number } | null | undefined,
                                   opts: { opening: boolean; vertical?: boolean; ms: number }): boolean;
/** Drops `el`'s slide in flight, its picture gone and the group shown again. */
export declare function settleSlideGhost(el: HTMLElement | null | undefined): void;

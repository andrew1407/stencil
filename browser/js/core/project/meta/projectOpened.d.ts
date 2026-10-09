/** The localStorage key holding { projectId: epochMs }. */
export declare const PROJECT_OPENED_KEY: string;
/** How many projects' open times are kept, newest first. */
export declare const PROJECT_OPENED_CAP: number;
/** When this browser last opened `id`, or 0 if never. */
export declare const projectOpenedAt: (id: string, all?: Record<string, number>) => number;
export declare const projectOpenedTable: () => Record<string, number>;
/** Stamp `id` as opened now. */
export declare const markProjectOpened: (id: string | null | undefined, now?: number) => void;

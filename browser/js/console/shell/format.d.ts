// The tables the console prints.
export declare const padColumns: (rows: unknown[][]) => string[];
export declare const projectsRows: (local?: object[], remote?: object[], opts?: { long?: boolean; activeId?: unknown }) => string[];
export declare const connectionsRows: (urls?: string[]) => string[];

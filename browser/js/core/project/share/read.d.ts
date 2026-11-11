// What a project row's Copy › entries read, each loaded only once it is picked.

export interface ShareRemoteRow { id: string; serverUrl: string; source?: string }
export interface ShareTarget { id?: string | number | null; remote?: ShareRemoteRow | null }

/** The layout as a download or a paste carries it; a server row reads its record alone. */
export declare const readShareLayout: (c: object, target: ShareTarget) => Promise<Record<string, unknown>>;
/** The whole `.stencil` text: record, original and layout. */
export declare const readShareProjectText: (c: object, target: ShareTarget) => Promise<string>;
/** The original's bytes and the layout as stored: what a picture of the project is drawn from; throws when it has no image. */
export declare const readShareRenderSource: (c: object, target: ShareTarget) => Promise<{ blob: Blob; layout: Record<string, unknown> }>;

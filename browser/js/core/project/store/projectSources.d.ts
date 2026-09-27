// A data-URL image source as the registry keeps it: a compact, stable reference whose full URL
// stays in the project's payload (layout.imageSource) and is resolved from there on demand.
import type { ProjectMeta, ProjectPayload, ProjectsStore, StorageBackend } from './projectsStore.js';

/** The prefix every reference starts with: `stencil-source:<media type>;<length>;<64-bit hex hash>`. */
export declare const SOURCE_REF_SCHEME: string;
/** cyrb53 with both 32-bit lanes kept: 16 hex digits over every UTF-16 unit of `s`. */
export declare const hash64: (s: string) => string;
export declare const isDataUrlSource: (s: unknown) => s is string;
export declare const isSourceRef: (s: unknown) => s is string;
/** A data URL's reference; any other value unchanged, so a reference maps to itself. */
export declare const sourceRef: <S>(s: S) => S | string;
/** The meta as its registry row carries it, in place: a data-URL source becomes its reference. */
export declare const withSourceRef: <M extends Partial<ProjectMeta>>(meta: M) => M;
/** The full URL when `full` is the data URL `stored` references, else `stored`. */
export declare const resolveSource: (stored: string | null | undefined, full: unknown) => string | null | undefined;
/** A stored project's source text; only a reference reads the payload. Null for an unknown id. */
export declare const storedSource: (store: ProjectsStore, id: string) => string | null;
/** The payload with a data-URL layout.imageSource down to its reference; the same object when nothing sheds. */
export declare const shedSource: (payload: ProjectPayload) => ProjectPayload;
/** What leaves this browser as a source (file, hand-off, facade, Links field): '' for a reference, else unchanged. */
export declare const portableSource: <S>(s: S) => S | '';
/** A server row's source over the local entry for the same project: the server's when http(s), else the local one. */
export declare const keptSource: (serverSrc: string | null | undefined, localSrc: string | null | undefined) => string;
/** The source a server may be sent: '' for a data URL or a reference. */
export declare const wireSource: (s: unknown) => string;
/** Inline data-URL sources become references, in place, the payload filled first where it lacks the text; true when a row changed. */
export declare const moveInlineSources: (storage: StorageBackend, rows: ProjectMeta[], payloadKey: (id: string) => string) => boolean;

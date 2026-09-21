import type { ProjectTransferController as C } from './projectTransferController.js';
/** A NEW server project from a local project's bytes + layout. Returns { link, proj, meta }. */
export declare function createServerFromLocal(c: C, id: string, address: string, name?: string | null): Promise<object>;
export declare function moveProjectToServer(c: C, id: string, address: string): Promise<object>;
export declare function copyProjectToServer(c: C, id: string, address: string, opts?: { name?: string }): Promise<object>;
export declare function moveProjectToLocal(c: C, meta: object): Promise<string>;
export declare function copyServerProjectToLocal(c: C, meta: object, opts?: { name?: string }): Promise<string>;
export declare function copyServerProjectToIncognito(c: C, meta: object, opts?: { newTab?: boolean }): Promise<unknown>;
export declare function blobToDataUrl(c: C, blob: Blob): Promise<string>;
export declare function importServerProjectToLocal(c: C, meta: object,
  opts?: { removeFromServer?: boolean; copy?: boolean; name?: string | null }): Promise<string>;

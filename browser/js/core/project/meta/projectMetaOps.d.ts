import type { ProjectTransferController as C } from '../transferController.js';
export declare function renameProject(c: C, id: string, name: string): object | null;
export declare function pushProjectFieldToServer(c: C, id: string, fields: object, failMsg: string): Promise<void>;
/** A linked project's current server version, or `fallback` on any error. */
export declare function currentRemoteVersion(c: C, conn: object, remoteId: string, fallback: number): Promise<number>;
export declare function setProjectColor(c: C, id: string, color: string): object | null;
export declare function setProjectKeywords(c: C, id: string, keywords: string | string[]): object | null;
export declare function setProjectDescription(c: C, id: string, description: string): object | null;
export declare function setProjectBlankColor(c: C, id: string, color: string): object | null;

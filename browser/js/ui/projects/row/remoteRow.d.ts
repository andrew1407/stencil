/** The thumbnail bytes of a server project: its result, else its original, else null (no request). */
export declare const remoteThumbBlob: (conn: { fetchFile(id: string, kind: 'original' | 'result'): Promise<Blob> }, meta: Record<string, unknown>) => Promise<Blob | null>;
/** Build one server-project row. The factory binds the modal's collaborators once. */
export function createRemoteRow(deps: Record<string, unknown>): (meta: Record<string, unknown>) => HTMLElement;

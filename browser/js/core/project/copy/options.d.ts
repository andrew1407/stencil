// "Make a copy" requests, shared by every entry point and the copyProject op (desktop twin:
// app/project/copy/ProjectCopy.cpp): the scopes, the ways a copy opens, and the rules that
// reduce a request to what can be honoured.

export type CopyScope = 'image' | 'layout' | 'project';
export type CopyOpen = 'none' | 'here' | 'newtab';

/** What a caller asks for; `local` only matters for a server-linked source. */
export interface CopyProjectRequest {
  what: CopyScope;
  open?: CopyOpen;
  incognito?: boolean;
  local?: boolean;
}

export interface SettledCopyOptions {
  what: CopyScope;
  open: CopyOpen;
  incognito: boolean;
  /** The copy is created on the source's server. */
  onServer: boolean;
  /** What of the request was dropped, or null. */
  note: string | null;
}

export declare const COPY_SCOPES: readonly CopyScope[];
export declare const COPY_OPENS: readonly CopyOpen[];
export declare const COPY_SCOPE_LABELS: Readonly<Record<CopyScope, string>>;
export declare const COPY_SCOPE_ICONS: Readonly<Record<CopyScope, string>>;
/** Throws on an unknown scope or open; incognito falls away unopened or on a server copy. */
export declare const settleCopyOptions: (req: CopyProjectRequest, ctx?: { serverSource?: boolean }) => SettledCopyOptions;

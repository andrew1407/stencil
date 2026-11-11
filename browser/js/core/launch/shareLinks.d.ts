// The Stencil links a project or a picture is shared by, built from the running app's page URL.

export type ShareLinkKind = 'name' | 'source' | 'server';
export interface ShareLink { kind: ShareLinkKind; url: string }

/** `<page>?project=<name>`: a project of this app by name. */
export declare const projectNameLink: (pageUrl: string, name: string) => string;
/** `<page>?source=<url>`: the picture at that URL, opened in this app. */
export declare const sourceLink: (pageUrl: string, source: string) => string;
/** `<page>?project=<server>/projects/<id>`: a server project. */
export declare const serverProjectLink: (pageUrl: string, serverUrl: string, id: string) => string;
/** Every link the project has a value for, in menu order (name, source, server). */
export declare const projectShareLinks: (pageUrl: string,
  facts?: { name?: string; source?: string; serverUrl?: string; remoteId?: string }) => ShareLink[];

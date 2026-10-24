// The launch query: `?key=value` pairs on the page URL, or the same pairs in the fragment, read
// once at boot and applied through the facade (console/launchQuery.js). Pure string work; the
// corpus is common/fixtures/deepLink/launchQuery.json.

/** The keys honoured only from the fragment carrier; in the query they are dropped with a warning. */
export declare const LAUNCH_QUERY_SECRET_KEYS: readonly ['token', 'llm-api-key'];

/** server+id beats project beats source beats blank; `frame` is seconds into a video, null when not given. */
export type LaunchPicture =
  | { kind: 'server'; url: string; id: string; version: number }
  /** `host[:port]/projects/<ref>` or a bare `<ref>` (id or name); `server` null = the launch's server, else a local project. */
  | { kind: 'project'; server: string | null; ref: string }
  | { kind: 'source'; url: string; frame: number | null }
  | { kind: 'blank'; color: string; width: number | null; height: number | null };

/** Why a pair was dropped: a credential in the query, unparsable JSON, or an unusable value. */
export type LaunchWarningCode = 'secret-in-query' | 'bad-json' | 'bad-value';
export interface LaunchWarning { code: LaunchWarningCode; key: string; }

export interface LaunchQuery {
  picture: LaunchPicture | null;
  /** The picture's name, and the project's. */
  name: string | null;
  /** Fetched from the URL, or inline JSON (`layout-raw`, or a `layout` that starts with `{`). */
  layout: { url: string } | { data: Record<string, unknown> } | null;
  incognito: boolean;
  /** '' = none; longer than LAUNCH.scriptMaxChars is dropped. */
  script: string;
  theme: 'dark' | 'light' | 'system' | null;
  /** Every key outside the grammar, camel-cased and coerced: the applier matches it to a stencil.settings setter. */
  settings: Record<string, string | number | boolean>;
  llm: { provider?: string; model?: string; baseUrl?: string; serverUrl?: string; apiKey?: string };
  chat: { open: boolean; dock: 'left' | 'right' | 'top' | 'bottom' | 'float' | null };
  /** `connect` repeats; a `token` pairs with the `connect` at the same position. */
  connect: { url: string; token: string }[];
  project: { description: string | null; keywords: string | null; color: string | null; expire: string | null };
  warnings: LaunchWarning[];
  /** The fragment carried pairs, so the applier strips it too. */
  fromHash: boolean;
  /** A picture, incognito or a script: the projects chooser stays closed. */
  opensSession: boolean;
}

/** true/yes/on and false/no/off become booleans, a number-shaped string a number, the rest stays text. */
export declare const coerceLaunchValue: (v: string) => string | number | boolean;
/** A `#a=b` fragment that is not a `#stencil=` / `#stencil-desktop=` hand-off. */
export declare const isLaunchQueryHash: (hash: string | null | undefined) => boolean;
/** The query that names nothing; a corpus vector's `expect` is merged over it. */
export declare const emptyLaunchQuery: () => LaunchQuery;
/** Null when neither carrier has a pair of this grammar; a bare `?open=<id>` is not one. */
export declare const parseLaunchQuery: (search?: string, hash?: string) => LaunchQuery | null;
/** A picture, connection, layout, look, assistant or chat key: a picture URL with grammar-looking parameters is not a link. */
export declare const isActionableLaunch: (q: LaunchQuery | null) => boolean;
/** A whole `http(s)://…?query#fragment` or `stencil://open?…` typed somewhere; null unless actionable. */
export declare const parseLaunchUrl: (url: string) => LaunchQuery | null;

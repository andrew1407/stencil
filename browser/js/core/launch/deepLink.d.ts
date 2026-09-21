// Project deep links, pure string helpers: the `?open=<id>` param a new tab consumes, the
// `#stencil=<JSON>` launch fragment, the `stencil://` desktop scheme, the Telegram start
// payload codec (shared golden vectors with desktop and bot), and the inbound validator.
import type { LayoutPayload } from '../layout.js';

export declare const OPEN_PARAM: 'open';
export declare const readOpenProjectId: (search?: string) => string | null;
export declare const buildOpenProjectUrl: (base: string, id: string) => string;
export declare const buildExternalLaunchUrl: (base: string, payload: LaunchPayload) => string;

export interface StencilSchemeFields {
  scheme?: string;
  server?: string;
  id?: string;
  version?: number | string;
  src?: string;
  layout?: string | object;
  frame?: number | string;
  incognito?: boolean;
}
/** server+id wins over src on the receiving side; empty fields are omitted. */
export declare const buildStencilSchemeUrl: (fields?: StencilSchemeFields) => string;

/** Telegram caps `?start=` payloads at 64 chars of [A-Za-z0-9_-]. */
export declare const TELEGRAM_START_LIMIT: 64;
/** "1" + base64url("host[:port]|projectId"); null when it would exceed the limit. */
export declare const encodeTelegramStartPayload: (serverUrl: string, projectId: string) => string | null;
export declare const buildTelegramLink: (botUsername: string, payload: string) => string;
export declare const buildDesktopBounceUrl: (browserBase: string | null | undefined, stencilUrl: string) => string;
/** The server's 32 MiB MaxBodyBytes — the largest inbound dataUrl accepted. */
export declare const LAUNCH_DATA_URL_MAX: number;

/** An inbound `#stencil=` fragment before validation. */
export interface LaunchPayload {
  server?: { url: string; id: string; version?: number };
  dataUrl?: string;
  src?: string;
  name?: string;
  crop?: object;
  noCrop?: boolean;
  page?: object;
  source?: string;
  resource?: string;
  open?: string;
  incognito?: boolean;
  layout?: LayoutPayload | object;
}

export interface NormalizedLaunchCommon {
  name: string | null;
  crop: Record<string, unknown> | null;
  noCrop: boolean;
  page: Record<string, unknown> | null;
  source: string | null;
  resource: string | null;
  open: string | null;
  incognito: boolean;
  layout: Record<string, unknown> | null;
}

/** Precedence when several image sources are present: server > dataUrl > src. */
export type NormalizedLaunch =
  | (NormalizedLaunchCommon & { kind: 'server'; server: { url: string; id: string; version: number } })
  | (NormalizedLaunchCommon & { kind: 'dataUrl'; dataUrl: string })
  | (NormalizedLaunchCommon & { kind: 'src'; src: string });

/** null for junk; prototype-pollution keys are dropped at the door. */
export declare const normalizeLaunchPayload: (payload: unknown) => NormalizedLaunch | null;
